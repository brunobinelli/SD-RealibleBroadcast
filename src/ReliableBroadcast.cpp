#include "ReliableBroadcast.h"

#include <algorithm>
#include <chrono>
#include <random>

// ── Global singletons ──────────────────────────────────────
EventBus          gBus;
ReliableBroadcast gAlgo;

// utilities

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
               .count();
}

std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

// event bus

void EventBus::push(const std::string& type, const std::string& json) {
    std::lock_guard<std::mutex> lk(mtx);
    queue.push_back({type, json});
    cv.notify_all();
}

std::vector<SseEvent> EventBus::since(size_t from) {
    std::lock_guard<std::mutex> lk(mtx);
    if (from >= queue.size()) return {};
    return {queue.begin() + from, queue.end()};
}

size_t EventBus::size() {
    std::lock_guard<std::mutex> lk(mtx);
    return queue.size();
}

// implementação do algoritmo -> parte publica da classe.

void ReliableBroadcast::setClients(const std::vector<ClientConfig>& cfg) {
    std::lock_guard<std::mutex> lk(stateMtx);
    clients = cfg;
    pushStateEvent();
}

void ReliableBroadcast::updateClient(int id, ClientStatus status, double lossRate) {
    std::lock_guard<std::mutex> lk(stateMtx);

    for (auto& c : clients) {
        if (c.id == id) { c.status = status; c.lossRate = lossRate; break; }
    }

    const std::string statusStr =
        status == ClientStatus::ONLINE  ? "online"  :
        status == ClientStatus::OFFLINE ? "offline" : "slow";

    emit("info",
         "{\"msg\":\"" + escapeJson(getClientName(id)) +
         " mudou status para " + statusStr +
         "\",\"clientId\":"    + std::to_string(id) + "}");

    pushStateEvent();
}

void ReliableBroadcast::broadcast(const std::string& content) {
    BroadcastMessage msg;
    msg.id      = nextMsgId++;
    msg.content = content;

    std::vector<ClientConfig> snap;
    {
        std::lock_guard<std::mutex> lk(stateMtx);
        snap = clients;
        for (auto& c : snap)
            msg.acks[c.id] = {c.id, AckState::PENDING, 0};
        messages.push_back(msg);
    }

    const int msgId = msg.id;

    emit("send",
         "{\"msgId\":"    + std::to_string(msgId) +
         ",\"content\":\"" + escapeJson(content)  +
         "\",\"total\":"  + std::to_string(snap.size()) + "}");

    std::this_thread::sleep_for(std::chrono::milliseconds(simDelay));

    std::vector<std::thread> threads;
    threads.reserve(snap.size());
    for (auto& c : snap)
        threads.emplace_back([this, msgId, c]() { deliverToClient(msgId, c); });
    for (auto& t : threads)
        if (t.joinable()) t.join();

    // Final accounting
    int confirmed = 0, failed = 0;
    {
        std::lock_guard<std::mutex> lk(stateMtx);
        for (auto& m : messages) {
            if (m.id != msgId) continue;
            for (auto& [cid, rec] : m.acks) {
                if (rec.state == AckState::CONFIRMED) ++confirmed;
                else                                  ++failed;
            }
            m.done    = true;
            m.success = (failed == 0);
            break;
        }
    }

    if (failed == 0) {
        emit("ok",
             "{\"msgId\":"     + std::to_string(msgId)     +
             ",\"confirmed\":" + std::to_string(confirmed) +
             ",\"failed\":0,\"msg\":\"ENTREGA CONFIáVEL CONFIRMADA\"}");
    } else {
        emit("fail",
             "{\"msgId\":"     + std::to_string(msgId)     +
             ",\"confirmed\":" + std::to_string(confirmed) +
             ",\"failed\":"    + std::to_string(failed)    +
             ",\"msg\":\"ENTREGA PARCIAL\"}");
    }

    {
        std::lock_guard<std::mutex> lk(stateMtx);
        pushStateEvent();
    }
}

std::string ReliableBroadcast::getStateJson() {
    std::lock_guard<std::mutex> lk(stateMtx);
    return buildStateJson();
}

// implmentação do algoritmo, parte privada

void ReliableBroadcast::deliverToClient(int msgId, const ClientConfig& client) {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<double> dist(0.0, 1.0);

    for (int attempt = 0; attempt <= maxRetries; ++attempt) {

        if (attempt > 0) {
            updateAckState(msgId, client.id, AckState::RETRYING, attempt);
            emit("retry",
                 "{\"msgId\":"       + std::to_string(msgId)      +
                 ",\"clientId\":"    + std::to_string(client.id)  +
                 ",\"clientName\":\"" + escapeJson(client.name)   +
                 "\",\"attempt\":"   + std::to_string(attempt) + "}");
            std::this_thread::sleep_for(std::chrono::milliseconds(simDelay / 2));
        }

        ClientStatus curStatus = ClientStatus::ONLINE;
        double       curLoss   = 0.0;
        {
            std::lock_guard<std::mutex> lk(stateMtx);
            for (auto& c : clients) {
                if (c.id == client.id) { curStatus = c.status; curLoss = c.lossRate; break; }
            }
        }

        updateAckState(msgId, client.id, AckState::SENDING, attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(simDelay));

        const bool offline = (curStatus == ClientStatus::OFFLINE);
        const bool dropped = (!offline && dist(rng) < curLoss);

        if (offline || dropped) {
            const std::string reason = offline
                ? client.name + " está OFFLINE — mensagem descartada"
                : "Perda na rede para " + client.name + " (loss rate " +
                  std::to_string(static_cast<int>(curLoss * 100)) + "%)";

            emit("lost",
                 "{\"msgId\":"       + std::to_string(msgId)     +
                 ",\"clientId\":"    + std::to_string(client.id) +
                 ",\"clientName\":\"" + escapeJson(client.name)  +
                 "\",\"reason\":\"" + escapeJson(reason)         +
                 "\",\"attempt\":"  + std::to_string(attempt) + "}");

            if (attempt == maxRetries) {
                updateAckState(msgId, client.id, AckState::FAILED, attempt);
                emit("fail",
                     "{\"msgId\":"       + std::to_string(msgId)     +
                     ",\"clientId\":"    + std::to_string(client.id) +
                     ",\"clientName\":\"" + escapeJson(client.name)  +
                     "\",\"msg\":\"Falha definitiva após "           +
                     std::to_string(maxRetries + 1) + " tentativas\"}");
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(ackTimeout / 3));
            continue;
        }

        const int procDelay = (curStatus == ClientStatus::SLOW)
                              ? simDelay * 3 : simDelay / 2;

        emit("recv",
             "{\"msgId\":"       + std::to_string(msgId)     +
             ",\"clientId\":"    + std::to_string(client.id) +
             ",\"clientName\":\"" + escapeJson(client.name)  +
             "\",\"content\":\"\"}");

        std::this_thread::sleep_for(std::chrono::milliseconds(procDelay));

        emit("ack_s",
             "{\"msgId\":"       + std::to_string(msgId)     +
             ",\"clientId\":"    + std::to_string(client.id) +
             ",\"clientName\":\"" + escapeJson(client.name) + "\"}");

        std::this_thread::sleep_for(std::chrono::milliseconds(simDelay / 2));

        updateAckState(msgId, client.id, AckState::CONFIRMED, attempt);

        emit("ack_r",
             "{\"msgId\":"       + std::to_string(msgId)     +
             ",\"clientId\":"    + std::to_string(client.id) +
             ",\"clientName\":\"" + escapeJson(client.name) + "\"}");

        return;  // Done — ACK confirmed
    }
}

void ReliableBroadcast::updateAckState(int msgId, int clientId,
                                       AckState st, int retries) {
    {
        std::lock_guard<std::mutex> lk(stateMtx);
        for (auto& m : messages) {
            if (m.id != msgId) continue;
            if (m.acks.count(clientId)) {
                m.acks[clientId].state   = st;
                m.acks[clientId].retries = retries;
            }
            break;
        }
    }

    const std::string stStr =
        st == AckState::SENDING   ? "sending"   :
        st == AckState::CONFIRMED ? "confirmed" :
        st == AckState::FAILED    ? "failed"    :
        st == AckState::RETRYING  ? "retrying"  : "pending";

    gBus.push("ack_update",
        "{\"msgId\":"    + std::to_string(msgId)    +
        ",\"clientId\":" + std::to_string(clientId) +
        ",\"state\":\""  + stStr +
        "\",\"retries\":" + std::to_string(retries) + "}");
}

void ReliableBroadcast::emit(const std::string& type, const std::string& json) {
    gBus.push(type, json);
}

std::string ReliableBroadcast::getClientName(int id) {
    for (auto& c : clients)
        if (c.id == id) return c.name;
    return "P" + std::to_string(id);
}

std::string ReliableBroadcast::buildStateJson() {
    std::string j = "{\"clients\":[";
    for (size_t i = 0; i < clients.size(); ++i) {
        const auto& c = clients[i];
        const std::string st =
            c.status == ClientStatus::ONLINE  ? "online"  :
            c.status == ClientStatus::OFFLINE ? "offline" : "slow";
        if (i) j += ",";
        j += "{\"id\":"        + std::to_string(c.id)    +
             ",\"name\":\""    + escapeJson(c.name)       +
             "\",\"status\":\"" + st                      +
             "\",\"lossRate\":" + std::to_string(c.lossRate) + "}";
    }

    j += "],\"messages\":[";
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& m = messages[i];
        if (i) j += ",";
        j += "{\"id\":"        + std::to_string(m.id)          +
             ",\"content\":\"" + escapeJson(m.content)          +
             "\",\"done\":"    + (m.done    ? "true" : "false") +
             ",\"success\":"   + (m.success ? "true" : "false") +
             ",\"acks\":{";
        bool first = true;
        for (auto& [cid, rec] : m.acks) {
            const std::string rs =
                rec.state == AckState::CONFIRMED ? "confirmed" :
                rec.state == AckState::FAILED    ? "failed"    :
                rec.state == AckState::RETRYING  ? "retrying"  :
                rec.state == AckState::SENDING   ? "sending"   : "pending";
            if (!first) j += ",";
            j += "\"" + std::to_string(cid) +
                 "\":{\"state\":\""  + rs   +
                 "\",\"retries\":"   + std::to_string(rec.retries) + "}";
            first = false;
        }
        j += "}}";
    }

    j += "],\"config\":{\"maxRetries\":" + std::to_string(maxRetries) +
         ",\"ackTimeout\":"              + std::to_string(ackTimeout) +
         ",\"simDelay\":"                + std::to_string(simDelay)   + "}}";
    return j;
}

void ReliableBroadcast::pushStateEvent() {
    gBus.push("state", buildStateJson());
}
#include "HttpServer.h"
#include "ReliableBroadcast.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

// POSIX sockets
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>


HttpServer::HttpServer(int port) : port_(port) {}

HttpServer::~HttpServer() {
    stop();
    if (serverFd_ >= 0) {
        close(serverFd_);
        serverFd_ = -1;
    }
}

void HttpServer::run() {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) { perror("socket"); return; }

    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); return;
    }
    if (listen(serverFd_, 32) < 0) {
        perror("listen"); return;
    }

    fcntl(serverFd_, F_SETFL, O_NONBLOCK);

    std::cout << "\n╔══════════════════════════════════════════╗\n"
              << "║  Difusão Confiável — Servidor HTTP/SSE   ║\n"
              << "╚══════════════════════════════════════════╝\n"
              << "  Porta  : " << port_ << "\n"
              << "  Abra   : visualizador.html no navegador\n"
              << "  Parar  : Ctrl+C\n\n";

    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);
        int clientFd = accept(serverFd_,
                              reinterpret_cast<sockaddr*>(&clientAddr),
                              &clientLen);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break;
        }
        // Each connection handled in its own thread
        std::thread(handleConnection, clientFd).detach();
    }

    std::cout << "\nServidor encerrado.\n";
}

void HttpServer::stop() {
    running_ = false;
}

// lidar com conexões
void HttpServer::handleConnection(int clientFd) {
    std::string raw = readRequest(clientFd);
    if (raw.empty()) { close(clientFd); return; }

    HttpRequest req = parseRequest(raw);

    
    if (req.method == "OPTIONS") {
        auto r = httpResp(204, "text/plain", "");
        send(clientFd, r.c_str(), r.size(), 0);
        close(clientFd);
        return;
    }

    // ── GET /events  →  SSE stream ───────────────────────────
    if (req.method == "GET" && req.path == "/events") {
        const std::string header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: keep-alive\r\n\r\n";
        send(clientFd, header.c_str(), header.size(), 0);

        std::string initEvt = "event: state\ndata: " + gAlgo.getStateJson() + "\n\n";
        send(clientFd, initEvt.c_str(), initEvt.size(), 0);

        size_t cursor = gBus.size();   // only deliver events from here on
        bool   alive  = true;

        while (alive) {
            std::vector<SseEvent> newEvents;
            {
                std::unique_lock<std::mutex> lk(gBus.mtx);
                gBus.cv.wait_for(lk, std::chrono::milliseconds(500),
                                 [&] { return gBus.queue.size() > cursor; });
                newEvents.assign(gBus.queue.begin() + cursor, gBus.queue.end());
                cursor = gBus.queue.size();
            }

            for (auto& e : newEvents) {
                std::string pkt = "event: " + e.type + "\ndata: " + e.data + "\n\n";
                if (send(clientFd, pkt.c_str(), pkt.size(), 0) <= 0) {
                    alive = false;
                    break;
                }
            }

            if (alive) {
                const char* hb = ": heartbeat\n\n";
                if (send(clientFd, hb, std::strlen(hb), 0) <= 0)
                    alive = false;
            }
        }

        close(clientFd);
        return;
    }

    // ── POST /broadcast ──────────────────────────────────────
    if (req.method == "POST" && req.path == "/broadcast") {
        std::string content = jsonGet(req.body, "message");
        if (content.empty()) {
            auto r = httpResp(400, "application/json", "{\"error\":\"missing message\"}");
            send(clientFd, r.c_str(), r.size(), 0);
            close(clientFd);
            return;
        }
        // Run the broadcast without blocking this HTTP response
        std::thread([content]() { gAlgo.broadcast(content); }).detach();

        auto r = httpResp(200, "application/json", "{\"ok\":true}");
        send(clientFd, r.c_str(), r.size(), 0);
        close(clientFd);
        return;
    }

    // ── POST /config ─────────────────────────────────────────
    if (req.method == "POST" && req.path == "/config") {
        auto mr = jsonGet(req.body, "maxRetries");
        auto at = jsonGet(req.body, "ackTimeout");
        auto sd = jsonGet(req.body, "simDelay");
        if (!mr.empty()) gAlgo.maxRetries = std::stoi(mr);
        if (!at.empty()) gAlgo.ackTimeout = std::stoi(at);
        if (!sd.empty()) gAlgo.simDelay   = std::stoi(sd);

        auto r = httpResp(200, "application/json", "{\"ok\":true}");
        send(clientFd, r.c_str(), r.size(), 0);
        close(clientFd);
        return;
    }

    // ── POST /client ─────────────────────────────────────────
    if (req.method == "POST" && req.path == "/client") {
        auto sid     = jsonGet(req.body, "id");
        auto sstatus = jsonGet(req.body, "status");
        auto sloss   = jsonGet(req.body, "lossRate");
        if (!sid.empty()) {
            int id = std::stoi(sid);
            ClientStatus st = ClientStatus::ONLINE;
            if      (sstatus == "offline") st = ClientStatus::OFFLINE;
            else if (sstatus == "slow")    st = ClientStatus::SLOW;
            double loss = sloss.empty() ? 0.0 : std::stod(sloss);
            gAlgo.updateClient(id, st, loss);
        }
        auto r = httpResp(200, "application/json", "{\"ok\":true}");
        send(clientFd, r.c_str(), r.size(), 0);
        close(clientFd);
        return;
    }

    // ── GET /state ───────────────────────────────────────────
    if (req.method == "GET" && req.path == "/state") {
        auto r = httpResp(200, "application/json", gAlgo.getStateJson());
        send(clientFd, r.c_str(), r.size(), 0);
        close(clientFd);
        return;
    }

    // ── 404 ──────────────────────────────────────────────────
    auto r = httpResp(400, "application/json", "{\"error\":\"not found\"}");
    send(clientFd, r.c_str(), r.size(), 0);
    close(clientFd);
}

//HTTP helpers

std::string HttpServer::readRequest(int fd) {
    std::string buf;
    char        tmp[4096];
    while (true) {
        int n = static_cast<int>(recv(fd, tmp, sizeof(tmp) - 1, 0));
        if (n <= 0) break;
        tmp[n] = '\0';
        buf   += tmp;
        if (buf.find("\r\n\r\n") != std::string::npos) break;
    }
    return buf;
}

HttpRequest HttpServer::parseRequest(const std::string& raw) {
    HttpRequest        req;
    std::istringstream ss(raw);
    std::string        line;

    std::getline(ss, line);
    std::istringstream fl(line);
    fl >> req.method >> req.path;

    while (std::getline(ss, line) && line != "\r" && !line.empty()) {
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 2);
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n'))
            val.pop_back();
        for (auto& c : key) c = static_cast<char>(tolower(c));
        req.headers[key] = val;
    }

    if (req.headers.count("content-length")) {
        size_t len = std::stoul(req.headers.at("content-length"));
        auto   pos = raw.find("\r\n\r\n");
        if (pos != std::string::npos && pos + 4 < raw.size())
            req.body = raw.substr(pos + 4, len);
    }
    return req;
}

std::string HttpServer::jsonGet(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    ++pos;
    while (pos < json.size() && json[pos] == ' ') ++pos;

    if (json[pos] == '"') {
        ++pos;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\') ++pos;
            val += json[pos++];
        }
        return val;
    }

    size_t end = pos;
    while (end < json.size() &&
           json[end] != ',' && json[end] != '}' && json[end] != ']')
        ++end;
    return json.substr(pos, end - pos);
}

std::string HttpServer::httpResp(int code,
                                  const std::string& contentType,
                                  const std::string& body,
                                  const std::string& extraHeaders) {
    const std::string status =
        code == 200 ? "200 OK"          :
        code == 204 ? "204 No Content"  : "400 Bad Request";

    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: "   + contentType              + "\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           "Access-Control-Allow-Headers: Content-Type\r\n"
           + extraHeaders +
           "Connection: close\r\n\r\n" + body;
}
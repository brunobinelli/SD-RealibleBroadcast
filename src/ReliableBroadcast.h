#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// utilities

long long   nowMs();
std::string escapeJson(const std::string& s);

// tipos de dominio

enum class ClientStatus { ONLINE, OFFLINE, SLOW };
enum class AckState     { PENDING, SENDING, CONFIRMED, FAILED, RETRYING };

struct ClientConfig {
    int          id;
    std::string  name;
    ClientStatus status   = ClientStatus::ONLINE;
    double       lossRate = 0.0;
};

struct AckRecord {
    int      clientId = -1;
    AckState state    = AckState::PENDING;
    int      retries  = 0;
};

struct BroadcastMessage {
    int         id;
    std::string content;
    bool        done    = false;
    bool        success = false;
    std::map<int, AckRecord> acks;
};

// eventos SSE

struct SseEvent {
    std::string type;
    std::string data;
};

// Event bus - fila consumida pelos SSE

class EventBus {
public:
    std::mutex              mtx;
    std::condition_variable cv;
    std::vector<SseEvent>   queue;

    void                  push(const std::string& type, const std::string& json);
    std::vector<SseEvent> since(size_t from);
    size_t                size();
};

extern EventBus gBus;   

// classe principal do algoritmo

class ReliableBroadcast {
public:

    std::mutex                    stateMtx;
    std::vector<ClientConfig>     clients;
    std::vector<BroadcastMessage> messages;
    std::atomic<int>              nextMsgId{1};

    
    int maxRetries = 3;
    int ackTimeout = 1500;  
    int simDelay   = 300;  

    void        setClients(const std::vector<ClientConfig>& cfg);
    void        updateClient(int id, ClientStatus status, double lossRate);
    void        broadcast(const std::string& content);
    std::string getStateJson();   

private:
    void        deliverToClient(int msgId, const ClientConfig& client);
    void        updateAckState(int msgId, int clientId, AckState st, int retries);
    void        emit(const std::string& type, const std::string& json);

    
    std::string getClientName(int id);
    
    std::string buildStateJson();
    
    void        pushStateEvent();
};

extern ReliableBroadcast gAlgo;   
#pragma once

#include <atomic>
#include <map>
#include <string>

//Estrutura de mensagem do sistema

struct HttpRequest {
    std::string                        method;
    std::string                        path;
    std::string                        body;
    std::map<std::string, std::string> headers;
};

// Classe do servidor.

class HttpServer {
public:
    explicit HttpServer(int port);
    ~HttpServer();

    // loop de execução
    void run();

    // chamada para encerrar execução
    void stop();

private:
    int               port_;
    int               serverFd_ = -1;
    std::atomic<bool> running_{true};

    // lida com conexões um a um
    static void handleConnection(int clientFd);

    // HTTP helpers
    static std::string readRequest(int fd);
    static HttpRequest parseRequest(const std::string& raw);

    // tratador de JSON
    static std::string jsonGet(const std::string& json, const std::string& key);

    static std::string httpResp(int code,
                                const std::string& contentType,
                                const std::string& body,
                                const std::string& extraHeaders = "");
};
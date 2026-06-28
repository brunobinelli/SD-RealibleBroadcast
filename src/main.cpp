#include "HttpServer.h"
#include "ReliableBroadcast.h"

#include <csignal>
#include <cstdlib>

// único ponteiro global responsável por lidar com o sinal que chega no servidor
static HttpServer* gServerPtr = nullptr;

static void sigHandler(int) {
    if (gServerPtr) gServerPtr->stop();
}

int main(int argc, char* argv[]) {
    const int port = (argc > 1) ? std::atoi(argv[1]) : 8765;

    std::signal(SIGINT,  sigHandler);
    std::signal(SIGTERM, sigHandler);
    std::signal(SIGPIPE, SIG_IGN);   

    // criação de todos os processos em estado base, todos online e sem packet loss.
    const std::vector<ClientConfig> defaultClients = {
        {1, "P1", ClientStatus::ONLINE, 0.0},
        {2, "P2", ClientStatus::ONLINE, 0.0},
        {3, "P3", ClientStatus::ONLINE, 0.0},
        {4, "P4", ClientStatus::ONLINE, 0.0},
        {5, "P5", ClientStatus::ONLINE, 0.0},
    };
    gAlgo.setClients(defaultClients);

    HttpServer server(port);
    gServerPtr = &server;
    server.run();  

    return 0;
}
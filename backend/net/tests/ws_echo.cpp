#include "wsserver.h"

#include "platform/eventloop.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

bool parsePort(const char *str, std::uint16_t *portOut)
{
    if (!str || *str == '\0') {
        return false;
    }
    char *end = nullptr;
    const unsigned long val = std::strtoul(str, &end, 10);
    if (!end || *end != '\0' || val > 65535UL) {
        return false;
    }
    *portOut = static_cast<std::uint16_t>(val);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    std::uint16_t requestedPort = 0;
    std::string token;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--port" && i + 1 < argc) {
            if (!parsePort(argv[++i], &requestedPort)) {
                std::cerr << "Invalid port value\n";
                return 1;
            }
        } else if (arg.rfind("--port=", 0) == 0) {
            if (!parsePort(arg.c_str() + 7, &requestedPort)) {
                std::cerr << "Invalid port value\n";
                return 1;
            }
        } else if (arg == "--token" && i + 1 < argc) {
            token = argv[++i] ? argv[i] : "";
        } else if (arg.rfind("--token=", 0) == 0) {
            token = arg.substr(8);
        }
    }

    // A message goes back to the client that sent it. One that starts with
    // "all:" goes to every client, and the text "clients?" is answered with
    // how many are connected.
    WsServer server(AnimeCore::String::fromUtf8(token));
    server.textReceived.connect([&server](WsServer::ClientId client, const std::string &data) {
        if (data == "clients?") {
            server.sendText(client, "clients:" + std::to_string(server.clientCount()));
        } else if (data.rfind("all:", 0) == 0) {
            server.sendText(data);
        } else {
            server.sendText(client, data);
        }
    });
    server.binaryReceived.connect([&server](WsServer::ClientId client, const std::string &data) {
        if (data.rfind("all:", 0) == 0) {
            server.sendBinary(data);
        } else {
            server.sendBinary(client, data);
        }
    });

    if (!server.listen(requestedPort)) {
        std::cerr << "Failed to listen on 127.0.0.1:" << requestedPort << "\n";
        return 1;
    }

    std::cout << "ANIMEAN_BACKEND_PORT=" << server.port() << std::endl;
    std::fflush(stdout);

    return EventLoop::instance().exec();
}

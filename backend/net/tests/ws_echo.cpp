#include "wsserver.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

#include <cstdio>
#include <iostream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    QCommandLineOption portOption(
        QStringLiteral("port"),
        QStringLiteral("Port to listen on (0 for automatic)."),
        QStringLiteral("port"),
        QStringLiteral("0"));
    QCommandLineOption tokenOption(
        QStringLiteral("token"),
        QStringLiteral("Required connection token."),
        QStringLiteral("token"),
        QString());

    parser.addOption(portOption);
    parser.addOption(tokenOption);
    parser.process(app);

    bool ok = false;
    const uint parsedPort = parser.value(portOption).toUInt(&ok);
    if (!ok || parsedPort > 65535) {
        std::cerr << "Invalid port value\n";
        return 1;
    }

    const quint16 requestedPort = static_cast<quint16>(parsedPort);
    const QString token = parser.value(tokenOption);

    WsServer server(token);
    QObject::connect(&server, &WsServer::textReceived, &server, &WsServer::sendText);
    QObject::connect(&server, &WsServer::binaryReceived, &server, &WsServer::sendBinary);

    if (!server.listen(requestedPort)) {
        std::cerr << "Failed to listen on 127.0.0.1:" << requestedPort << "\n";
        return 1;
    }

    std::cout << "ANIMEAN_BACKEND_PORT=" << server.port() << std::endl;
    std::fflush(stdout);

    return app.exec();
}

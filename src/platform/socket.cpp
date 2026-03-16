/**
 * @file socket.cpp
 * @brief Cross-platform socket implementation
 */

#include "socket.h"
#include <cstring>
#include <iostream>

#ifdef _WIN32
    static bool g_winsockInitialized = false;
#endif

namespace tls104 {

bool socketInit() {
#ifdef _WIN32
    if (!g_winsockInitialized) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "[Socket] WSAStartup failed: " << result << std::endl;
            return false;
        }
        g_winsockInitialized = true;
    }
#endif
    return true;
}

void socketCleanup() {
#ifdef _WIN32
    if (g_winsockInitialized) {
        WSACleanup();
        g_winsockInitialized = false;
    }
#endif
}

SocketType socketCreate() {
    SocketType sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == SOCKET_INVALID) {
        std::cerr << "[Socket] Failed to create socket" << std::endl;
    }
    return sock;
}

bool socketConnect(SocketType sock, const std::string& host, int port) {
    if (sock == SOCKET_INVALID) return false;

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));

    // Try to parse as IP first, then as hostname
#ifdef _WIN32
    serverAddr.sin_addr.s_addr = inet_addr(host.c_str());
    if (serverAddr.sin_addr.s_addr == INADDR_NONE) {
        // Need to resolve hostname
        hostent* he = gethostbyname(host.c_str());
        if (he == nullptr) {
            std::cerr << "[Socket] Failed to resolve host: " << host << std::endl;
            return false;
        }
        memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
#else
    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        // Need to resolve hostname
        hostent* he = gethostbyname(host.c_str());
        if (he == nullptr) {
            std::cerr << "[Socket] Failed to resolve host: " << host << std::endl;
            return false;
        }
        memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
#endif

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[Socket] Failed to connect to " << host << ":" << port << std::endl;
        return false;
    }

    return true;
}

bool socketListen(SocketType sock, int port) {
    if (sock == SOCKET_INVALID) return false;

    // Set SO_REUSEADDR
    int opt = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[Socket] Failed to bind to port " << port << std::endl;
        return false;
    }

    if (listen(sock, 5) < 0) {
        std::cerr << "[Socket] Failed to listen on port " << port << std::endl;
        return false;
    }

    return true;
}

SocketType socketAccept(SocketType sock) {
    if (sock == SOCKET_INVALID) return SOCKET_INVALID;

    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    SocketType clientSock = accept(sock, (struct sockaddr*)&clientAddr, &addrLen);

    return clientSock;
}

int socketSend(SocketType sock, const uint8_t* data, int len) {
    if (sock == SOCKET_INVALID) return -1;
    return send(sock, reinterpret_cast<const char*>(data), len, 0);
}

int socketRecv(SocketType sock, uint8_t* buffer, int buflen) {
    if (sock == SOCKET_INVALID) return -1;
    return recv(sock, reinterpret_cast<char*>(buffer), buflen, 0);
}

void socketClose(SocketType sock) {
    if (sock != SOCKET_INVALID) {
#ifdef _WIN32
        shutdown(sock, SD_BOTH);
#else
        shutdown(sock, SHUT_RDWR);
#endif
        SOCKET_CLOSE(sock);
    }
}

bool socketSetNonBlocking(SocketType sock) {
    if (sock == SOCKET_INVALID) return false;

#ifdef _WIN32
    unsigned long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) >= 0;
#endif
}

bool socketIsValid(SocketType sock) {
    return sock != SOCKET_INVALID;
}

} // namespace tls104

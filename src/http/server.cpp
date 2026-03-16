/**
 * @file server.cpp
 * @brief Simple HTTP server implementation
 */

#include "server.h"
#include "../platform/socket.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
    #include <filesystem>
#else
    #include <sys/stat.h>
#endif

namespace tls104 {

HttpServer::HttpServer(int port)
    : port_(port), serverFd_(SOCKET_INVALID), running_(false), documentRoot_("./web") {
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
    if (running_) return true;

    if (!socketInit()) {
        std::cerr << "[HTTP] Failed to initialize sockets" << std::endl;
        return false;
    }

    serverFd_ = socketCreate();
    if (!socketIsValid(serverFd_)) {
        std::cerr << "[HTTP] Failed to create socket" << std::endl;
        return false;
    }

    // Set SO_REUSEADDR
    int opt = 1;
#ifdef _WIN32
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(serverFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[HTTP] Failed to bind to port " << port_ << std::endl;
        socketClose(serverFd_);
        serverFd_ = SOCKET_INVALID;
        return false;
    }

    if (listen(serverFd_, 5) < 0) {
        std::cerr << "[HTTP] Failed to listen" << std::endl;
        socketClose(serverFd_);
        serverFd_ = SOCKET_INVALID;
        return false;
    }

    running_ = true;
    acceptThread_ = std::thread(&HttpServer::acceptLoop, this);

    std::cout << "[HTTP] Server started on port " << port_ << std::endl;
    return true;
}

void HttpServer::stop() {
    if (!running_) return;

    running_ = false;

    // Close server socket to unblock accept
    if (socketIsValid(serverFd_)) {
        socketClose(serverFd_);
        serverFd_ = SOCKET_INVALID;
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

void HttpServer::setDocumentRoot(const std::string& root) {
    documentRoot_ = root;
}

void HttpServer::acceptLoop() {
    while (running_) {
        SocketType clientFd = socketAccept(serverFd_);
        if (!socketIsValid(clientFd)) {
            if (running_) {
                std::cerr << "[HTTP] Accept failed" << std::endl;
            }
            continue;
        }

        // Handle in a separate thread
        std::thread([this, clientFd]() {
            handleClient(clientFd);
            socketClose(clientFd);
        }).detach();
    }
}

void HttpServer::handleClient(int clientFd) {
    // Read request
    char buffer[4096] = {0};
    int n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return;

    std::string request(buffer, n);

    // Parse request line
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;

    // Default to index.html
    if (path == "/") {
        path = "/index.html";
    }

    // Read file
    std::string fullPath = documentRoot_ + path;
    std::string content = readFile(fullPath);

    // Build response
    std::ostringstream response;
    std::string contentType = getContentType(path);

    if (!content.empty()) {
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: " << contentType << "\r\n";
        response << "Content-Length: " << content.size() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << content;
    } else {
        std::string notFound = "<html><body><h1>404 Not Found</h1></body></html>";
        response << "HTTP/1.1 404 Not Found\r\n";
        response << "Content-Type: text/html\r\n";
        response << "Content-Length: " << notFound.size() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << notFound;
    }

    std::string responseStr = response.str();
    send(clientFd, responseStr.c_str(), responseStr.size(), 0);
}

std::string HttpServer::readFile(const std::string& path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string HttpServer::getContentType(const std::string& path) const {
    std::string ext = path.substr(path.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";

    return "text/plain";
}

} // namespace tls104

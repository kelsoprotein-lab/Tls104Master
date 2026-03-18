/**
 * @file server.cpp
 * @brief Simple HTTP server implementation with API support
 */

#include "server.h"
#include "../platform/socket.h"
#include "embedded_web.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace tls104 {

HttpServer::HttpServer(int port)
    : port_(port), serverFd_(static_cast<SocketType>(0)), running_(false), apiHandler_(nullptr) {
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

    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
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
        serverFd_ = static_cast<SocketType>(0);
        return false;
    }

    if (listen(serverFd_, 5) < 0) {
        std::cerr << "[HTTP] Failed to listen" << std::endl;
        socketClose(serverFd_);
        serverFd_ = static_cast<SocketType>(0);
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

    // Wake up all SSE clients so their threads can exit
    {
        std::lock_guard<std::mutex> lock(sseClientsMutex_);
        for (auto& client : sseClients_) {
            client->alive = false;
            client->cv.notify_all();
        }
    }

    // Close server socket to unblock accept
    if (socketIsValid(serverFd_)) {
        socketClose(serverFd_);
        serverFd_ = static_cast<SocketType>(0);
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

void HttpServer::setAPIHandler(HttpHandler handler) {
    apiHandler_ = handler;
}

void HttpServer::broadcast(const std::string& json) {
    std::lock_guard<std::mutex> lock(sseClientsMutex_);
    for (auto& client : sseClients_) {
        if (client->alive) {
            std::lock_guard<std::mutex> cLock(client->mutex);
            client->queue.push(json);
            client->cv.notify_one();
        }
    }
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

void HttpServer::handleClient(SocketType clientFd) {
    // Read request
    char buffer[4096] = {0};
    int n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return;

    std::string request(buffer, n);

    // Parse request line
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;

    // Get request body if present
    std::string body;
    size_t bodyPos = request.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        body = request.substr(bodyPos + 4);
    }

    // Default to index.html
    if (path == "/") {
        path = "/index.html";
    }

    std::string response;

    // Handle API requests
    if (path.find("/api/") == 0 && apiHandler_) {
        response = handleAPI(path, method, body);
    } else if (path == "/events") {
        handleSSE(clientFd);
        return;
    } else if (path == "/index.html") {
        // Serve embedded index.html from compiled-in byte array
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n";
        resp << "Content-Type: text/html\r\n";
        resp << "Content-Length: " << embedded_index_html_size << "\r\n";
        resp << "Access-Control-Allow-Origin: *\r\n";
        resp << "Connection: close\r\n";
        resp << "\r\n";
        resp.write(reinterpret_cast<const char*>(embedded_index_html), embedded_index_html_size);
        response = resp.str();
    } else {
        std::string notFound = "<html><body><h1>404 Not Found</h1></body></html>";
        std::ostringstream resp;
        resp << "HTTP/1.1 404 Not Found\r\n";
        resp << "Content-Type: text/html\r\n";
        resp << "Content-Length: " << notFound.size() << "\r\n";
        resp << "Access-Control-Allow-Origin: *\r\n";
        resp << "Connection: close\r\n";
        resp << "\r\n";
        resp << notFound;
        response = resp.str();
    }

    if (!response.empty()) {
        send(clientFd, response.c_str(), static_cast<int>(response.size()), 0);
    }
}

void HttpServer::handleSSE(SocketType clientFd) {
    // Send SSE headers
    std::string header = "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    send(clientFd, header.c_str(), static_cast<int>(header.size()), 0);

    // Register this client with its own queue
    auto client = std::make_shared<SSEClient>();
    {
        std::lock_guard<std::mutex> lock(sseClientsMutex_);
        sseClients_.push_back(client);
    }

    // Event loop: wait for messages, send to this client
    while (running_ && client->alive) {
        std::vector<std::string> msgs;
        {
            std::unique_lock<std::mutex> lock(client->mutex);
            client->cv.wait_for(lock, std::chrono::seconds(15), [&]() {
                return !client->queue.empty() || !running_ || !client->alive;
            });
            while (!client->queue.empty()) {
                msgs.push_back(client->queue.front());
                client->queue.pop();
            }
        }

        if (msgs.empty()) {
            // Send SSE comment as heartbeat to detect dead connections
            const char* heartbeat = ":heartbeat\r\n\r\n";
            int sent = send(clientFd, heartbeat, static_cast<int>(strlen(heartbeat)), 0);
            if (sent <= 0) break;
            continue;
        }

        for (const auto& m : msgs) {
            std::string frame = "data: " + m + "\r\n\r\n";
            int sent = send(clientFd, frame.c_str(), static_cast<int>(frame.size()), 0);
            if (sent <= 0) goto cleanup;
        }
    }

cleanup:
    // Remove this client from the list
    client->alive = false;
    std::lock_guard<std::mutex> lock(sseClientsMutex_);
    sseClients_.erase(
        std::remove(sseClients_.begin(), sseClients_.end(), client),
        sseClients_.end());
}

std::string HttpServer::handleAPI(const std::string& path, const std::string& method, const std::string& body) {
    if (apiHandler_) {
        return apiHandler_(path, method, body);
    }

    // Default response
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: 2\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "\r\n"
           "{}";
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

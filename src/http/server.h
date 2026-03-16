/**
 * @file server.h
 * @brief Simple HTTP server for serving Web UI
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace tls104 {

/**
 * @brief HTTP request handler
 */
using HttpHandler = std::function<std::string(const std::string& path, const std::string& method)>;

/**
 * @brief Simple HTTP server
 */
class HttpServer {
public:
    /**
     * @brief Constructor
     * @param port Port to listen on
     */
    explicit HttpServer(int port);

    /**
     * @brief Destructor
     */
    ~HttpServer();

    /**
     * @brief Start the server
     * @return true on success
     */
    bool start();

    /**
     * @brief Stop the server
     */
    void stop();

    /**
     * @brief Set document root directory
     * @param root Path to web files
     */
    void setDocumentRoot(const std::string& root);

    /**
     * @brief Check if running
     */
    bool isRunning() const { return running_; }

    /**
     * @brief Get the port
     */
    int getPort() const { return port_; }

private:
    void acceptLoop();
    void handleClient(int clientFd);
    std::string readFile(const std::string& path) const;
    std::string getContentType(const std::string& path) const;

    int port_;
    std::string documentRoot_;
    int serverFd_;
    std::atomic<bool> running_;
    std::thread acceptThread_;
    std::mutex mutex_;
};

} // namespace tls104

#endif // HTTP_SERVER_H

/**
 * @file main.cpp
 * @brief TLS104 Master - Windows Desktop Edition
 * @brief Main entry point with WebView2 integration
 */

#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>

// Platform-specific headers
#ifdef _WIN32
    #include <windows.h>
    #include "web/webview2.h"
#else
    #include <unistd.h>
#endif

#include "platform/socket.h"
#include "ipc/bridge.h"
#include "http/server.h"

namespace tls104 {

// Forward declarations
class StationManager;

// Global state
static std::atomic<bool> g_running{true};
static std::unique_ptr<HttpServer> g_httpServer;
static std::unique_ptr<IPCBridge> g_ipcBridge;
static std::unique_ptr<StationManager> g_stationManager;

#ifdef _WIN32
// WebView2 wrapper
class WebView2Window {
public:
    WebView2Window() : webview_(nullptr) {}

    bool create(HWND parent) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            std::cerr << "[WebView2] CoInitializeEx failed" << std::endl;
            return false;
        }

        // Create WebView2 environment
        hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, nullptr, nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, parent](HRESULT result, ICoreWebView2Environment* env) {
                    if (FAILED(result)) {
                        std::cerr << "[WebView2] Create environment failed" << std::endl;
                        return;
                    }

                    env->CreateCoreWebView2WindowAsync(parent,
                        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2WindowCompletedHandler>(
                            [this](HRESULT result, ICoreWebView2* webview) {
                                if (FAILED(result)) {
                                    std::cerr << "[WebView2] Create window failed" << std::endl;
                                    return;
                                }
                                webview_ = webview;
                                initWebView();
                            }).Get());
                }).Get());

        return true;
    }

    void navigate(const std::string& url) {
        if (webview_) {
            webview_->Navigate(std::wstring(url.begin(), url.end()).c_str());
        }
    }

    void sendMessage(const std::string& json) {
        if (webview_) {
            std::wstring wjson(json.begin(), json.end());
            webview_->PostWebMessageAsJson(wjson.c_str());
        }
    }

    void resize(int width, int height) {
        if (webview_) {
            webview_->put_Visible(true);
            webview_->put_IsVisible(true);
        }
    }

private:
    void initWebView() {
        if (!webview_) return;

        // Enable devtools
        webview_->put_IsDevToolsEnabled(true);

        // Set message handler for communication from JS
        webview_->AddWebMessageReceivedFilter(
            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
                    wil::unique_cotaskmem_string message;
                    args->TryGetWebMessageAsString(&message);
                    if (message) {
                        std::string msg(message.get(), message.length());
                        std::cout << "[WebView2] Received: " << msg << std::endl;
                        // Forward to IPC bridge
                        if (g_ipcBridge) {
                            g_ipcBridge->handleMessage(msg);
                        }
                    }
                    return S_OK;
                }).Get(), IID_ICoreWebView2WebMessageReceivedFilter);

        std::cout << "[WebView2] Initialized" << std::endl;
    }

    ICoreWebView2* webview_;
};

static WebView2Window* g_webview = nullptr;
#endif

// Station manager stub (to be implemented)
class StationManager {
public:
    void addStation(const StationConfig& config) {
        std::cout << "[StationManager] Adding station: " << config.id << std::endl;
        // TODO: Implement IEC 104 connection
    }

    void removeStation(const std::string& stationId) {
        std::cout << "[StationManager] Removing station: " << stationId << std::endl;
        // TODO: Implement
    }

    void sendInterrogation(const std::string& stationId, int ca) {
        std::cout << "[StationManager] Send interrogation to: " << stationId << std::endl;
    }

    void sendClockSync(const std::string& stationId, int ca) {
        std::cout << "[StationManager] Send clock sync to: " << stationId << std::endl;
    }

    void sendControl(const std::string& stationId, const ControlCommand& cmd) {
        std::cout << "[StationManager] Send control to: " << stationId << " IOA: " << cmd.ioa << std::endl;
    }
};

// Signal handler
void signalHandler(int signal) {
    std::cout << "\n[Main] Shutting down..." << std::endl;
    g_running = false;
}

} // namespace tls104

// Main
int main(int argc, char* argv[]) {
    using namespace tls104;

    std::cout << "======================================" << std::endl;
    std::cout << "  TLS104 Master - Windows Desktop" << std::endl;
    << "======================================" << std::endl;

    // Signal handling
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Parse arguments
    int httpPort = 8080;
    bool headless = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            httpPort = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: tls104_master_win [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -p <port>    HTTP server port (default: 8080)" << std::endl;
            std::cout << "  --headless   Run without GUI (for testing)" << std::endl;
            std::cout << "  --help       Show this help" << std::endl;
            return 0;
        }
    }

    // Initialize IPC Bridge
    g_ipcBridge = std::make_unique<IPCBridge>();

    // Set IPC callbacks
    g_ipcBridge->setCallback(new StationManager());

    // Start HTTP server
    g_httpServer = std::make_unique<HttpServer>(httpPort);
    g_httpServer->setDocumentRoot("./web");

    if (!g_httpServer->start()) {
        std::cerr << "[Main] Failed to start HTTP server" << std::endl;
        return 1;
    }

    std::string url = "http://localhost:" + std::to_string(httpPort);

#ifdef _WIN32
    if (!headless) {
        std::cout << "[Main] Starting WebView2..." << std::endl;

        // Create window class
        WNDCLASSA wc = {};
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "TLS104Master";
        RegisterClassA(&wc);

        // Create window
        HWND hwnd = CreateWindowA(
            "TLS104Master",
            "TLS104 Master",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            1200, 800,
            nullptr, nullptr,
            wc.hInstance,
            nullptr
        );

        if (!hwnd) {
            std::cerr << "[Main] Failed to create window" << std::endl;
            return 1;
        }

        // Create WebView2
        g_webview = new WebView2Window();
        if (!g_webview->create(hwnd)) {
            std::cerr << "[Main] Failed to create WebView2" << std::endl;
            std::cerr << "[Main] Make sure WebView2 Runtime is installed" << std::endl;
            delete g_webview;
            g_webview = nullptr;
            // Fall back to headless mode
            headless = true;
        } else {
            // Set IPC send callback
            g_ipcBridge->setSendCallback([g_webview](const std::string& json) {
                g_webview->sendMessage(json);
            });

            ShowWindow(hwnd, SW_SHOWNORMAL);
            g_webview->navigate(url);
            g_webview->resize(1200, 800);

            // Message loop
            MSG msg;
            while (g_running && GetMessageA(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }

            delete g_webview;
        }
    }
#else
    // Non-Windows: just run HTTP server
    std::cout << "[Main] Running in headless mode (non-Windows)" << std::endl;
#endif

    if (headless || !g_running) {
        // Headless mode: just run HTTP server
        std::cout << "[Main] Open browser at: " << url << std::endl;
        std::cout << "[Main] Press Ctrl+C to exit" << std::endl;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Cleanup
    std::cout << "[Main] Cleaning up..." << std::endl;
    g_httpServer.reset();
    g_ipcBridge.reset();

    std::cout << "[Main] Goodbye!" << std::endl;
    return 0;
}

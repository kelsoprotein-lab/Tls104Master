# Tls104Master-win

Windows 桌面版 IEC 60870-5-104 主站，基于 C++ + WebView2 实现。

## 功能特性

- **多从站管理** - 支持同时连接多个 IEC 104 从站
- **TLS 安全连接** - 支持 SSL/TLS 加密通信
- **实时监控** - 遥信、遥测数据实时显示
- **报文监控** - 原始报文十六进制显示
- **遥控操作** - 支持单点、双点遥控及调节命令
- **现代 UI** - 基于 Vue.js + Element Plus

## 系统要求

- Windows 10/11 (需要 WebView2 运行时)
- CMake >= 3.14
- C++17 编译器 (MSVC, MinGW, 或 Clang)
- lib60870 依赖
- mbedTLS (可选，用于 TLS 支持)

## 构建步骤

### 1. 安装依赖

```bash
# 安装 Visual Studio Build Tools
# 下载并安装: https://visualstudio.microsoft.com/visual-cpp-build-tools/

# 安装 CMake
# 下载: https://cmake.org/download/
```

### 2. 编译

```bash
# 创建构建目录
mkdir build
cd build

# 配置 (使用 Visual Studio Generator)
cmake .. -G "Visual Studio 17 2022" -A x64

# 或者使用 Ninja
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build . --config Release
```

### 3. 运行

```bash
# 编译完成后，运行可执行文件
./tls104_master_win.exe
```

程序会自动打开系统浏览器访问 http://localhost:8080

## 使用方法

1. 启动程序
2. 在浏览器中打开 http://localhost:8080
3. 点击"添加从站"按钮
4. 输入从站地址、端口、TLS 配置等
5. 点击"总召唤"获取数据
6. 监控实时数据变化

## 命令行选项

```
-p <port>    HTTP 服务器端口 (默认: 8080)
--headless   无 GUI 模式 (仅运行 Web 服务器)
--help       显示帮助
```

## 项目结构

```
Tls104Master-win/
├── src/
│   ├── main.cpp          # 主入口 + WebView2
│   ├── platform/         # 跨平台 Socket
│   ├── ipc/             # IPC 桥接
│   └── http/             # HTTP 服务器
├── web/                  # Vue.js 前端
├── CMakeLists.txt
└── README.md
```

## 技术栈

- **后端**: C++17, lib60870, mbedTLS
- **前端**: Vue 3, Element Plus
- **桌面**: WebView2 (Windows)
- **构建**: CMake

## 许可证

Apache License 2.0

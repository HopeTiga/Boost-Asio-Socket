# AsioCoroutineServer

## 项目简介

本项目为基于 C++ 和 Boost.Asio 实现的高性能服务器，支持多线程消息处理、会话管理、消息队列等功能。适用于学习网络编程、服务器架构设计等场景。

## 主要特性

- 基于 Boost.Asio 的异步网络通信
- 支持多线程消息处理
- 消息队列与对象池优化
- 支持会话管理与消息回调机制
- 代码结构清晰，易于扩展

## 目录结构
+-------------------+                                                                                                                                                                                                               
|   AsioCoroutine   |                                                                                                                                                                                                                        
+--------+----------+                                                                                                                                                                                                                        
         |
         v
+-------------------+
|   CServer (主控)  |
+--------+----------+
         |
         v
+-------------------+
| AsioIOServicePool |
+--------+----------+
         |
         v
+-------------------+
|    CSession       |
+--------+----------+
         |
         v
+-------------------+
|  BufferPool       |  <---+
+--------+----------+      |
         |                 |
         v                 |
+-------------------+      |
|  MessageNodes     |------+
+--------+----------+
         |
         v
+-------------------+
|   LogicSystem     |
+--------+----------+
         |
         v
+------------------------+
| SessionSendThread      |
| (仅供LogicSystem回调用)|
+------------------------+

## 编译与运行

### 依赖

- C++17 或以上
- Boost 库（主要用到 Boost.Asio、Boost.Lockfree 等）
- CMake 或 Visual Studio（推荐使用 VS 工程文件 `AsioCoroutineServer.vcxproj`）

### 编译步骤

1. 安装 Boost 库，并配置好开发环境。
2. 使用 Visual Studio 打开 `ChatServer.vcxproj` 工程文件，编译生成可执行文件。
3. 或者使用 CMake 自行配置编译（需自行编写 CMakeLists.txt）。

### 运行

1. 配置 `config.ini` 文件，设置服务器参数。
2. 运行编译生成的 `AsioCoroutineServer.exe`。

## 主要模块说明

- `LogicSystem`：核心逻辑系统，负责消息分发与线程管理。
- `CSession`：会话管理，处理客户端连接与消息收发。
- `MessageNodes`：消息节点与消息队列管理。
- `BufferPool`：内存池，优化消息缓冲区分配与释放。
- `ConfigMgr`：配置管理，读取服务器配置。

## 许可证

本项目采用 MIT License，详见 LICENSE.txt。

## 致谢

感谢 Boost 社区提供的高质量 C++ 库。

---
如需进一步了解或贡献代码，请提交 Issue 或 Pull Request。

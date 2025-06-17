# AsioCoroutine

## 项目简介
AsioCoroutine 是一个基于 C++20 协程和 Boost.Asio 构建的高性能异步网络服务器框架。该项目采用现代 C++ 设计理念，支持动态线程池、系统监控、内存优化等特性，适用于高并发网络应用开发。

## 核心特性

### 🚀 高性能异步架构
- **C++20 协程**：基于 `std::coroutine` 实现无栈协程，减少上下文切换开销
- **Boost.Asio**：异步 I/O 操作，支持高并发连接
- **动态 I/O 线程池**：根据系统负载自动调整 I/O 线程数量
- **无锁消息队列**：使用 `moodycamel::ConcurrentQueue` 实现高效消息传递

### 📊 智能系统监控
- **实时系统监控**：CPU、内存、网络 I/O 压力监测
- **自适应线程管理**：根据负载动态增减处理线程
- **性能指标收集**：消息处理时间、队列压力等关键指标

### 🔧 内存优化
- **AVX2 优化内存拷贝**：使用 SIMD 指令集加速数据传输
- **智能内存管理**：支持内存池和普通分配两种方式
- **零拷贝优化**：减少不必要的数据复制

### 🛡️ 高可靠性
- **异常安全**：全面的异常处理和资源管理
- **会话生命周期管理**：安全的连接建立和断开
- **内存泄漏防护**：RAII 和智能指针确保资源正确释放

## 架构设计

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   main.cpp      │───▶│   CServer        │───▶│   CSession      │
│   (入口程序)     │    │   (服务器核心)    │    │   (会话管理)     │
└─────────────────┘    └──────────────────┘    └─────────────────┘
                                │                        │
                                ▼                        ▼
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│ AsioProactors   │◀───│SystemMonitor     │    │ MessageNodes    │
│ (I/O线程池)     │    │ (系统监控)       │    │ (消息节点)      │
└─────────────────┘    └──────────────────┘    └─────────────────┘
         │                       │                        │
         ▼                       ▼                        ▼
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│boost::asio      │    │AdvancedSystem    │    │ LogicSystem     │
│(异步I/O)       │    │ Monitor          │    │ (逻辑处理)      │
└─────────────────┘    │ (高级监控)       │    └─────────────────┘
                       └──────────────────┘             │
                                                        ▼
                                               ┌─────────────────┐
                                               │SystemCoroutine  │
                                               │(协程调度)       │
                                               └─────────────────┘
```

## 主要模块

### 核心网络层
- **`CServer`**: 服务器主类，负责连接接受和会话管理
- **`CSession`**: 会话类，处理单个客户端连接的消息收发
- **`AsioProactors`**: I/O 线程池，根据负载动态调整线程数量

### 消息处理层
- **`LogicSystem`**: 消息处理系统，支持动态线程池和协程调度
- **`MessageNodes`**: 消息节点定义，支持多种内存分配策略
- **`SystemCoroutine`**: 协程封装，提供异步任务调度

### 系统监控层
- **`AdvancedSystemMonitor`**: 跨平台系统监控，支持 Windows/Linux/macOS
- **`MessagePressureMetrics`**: 消息压力指标收集和分析

### 工具层
- **`Utils`**: 日志系统和通用工具函数
- **`FastMemcpy_Avx`**: AVX2 优化的内存拷贝实现
- **`concurrentqueue`**: 高性能无锁并发队列

## 快速开始

### 环境要求
- **编译器**: 支持 C++20 的编译器 (GCC 10+, Clang 12+, MSVC 2022+)
- **依赖库**: 
  - Boost 1.75+ (asio, system, uuid)
  - CMake 3.15+ (可选)

### 编译步骤

#### Windows (Visual Studio)
```bash
# 克隆项目
git clone <repository-url>
cd AsioCoroutine

# 使用 Visual Studio 打开 AsioCoroutine.sln
# 或使用 MSBuild
msbuild AsioCoroutine.vcxproj /p:Configuration=Release
```

#### Linux/macOS
```bash
# 安装依赖 (Ubuntu/Debian)
sudo apt-get install libboost-all-dev cmake

# 编译项目
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 配置文件
创建 `config.ini` 文件：
```ini
[SelfServer]
Host=0.0.0.0
Port=8080

[Chatservers]
Name=server1,server2,server3
```

### 运行
```bash
./AsioCoroutine
```

## 性能特性

### 并发能力
- **连接数**: 支持数万并发连接
- **吞吐量**: 优化的消息处理流水线
- **延迟**: 微秒级消息处理延迟

### 内存优化
- **SIMD 加速**: AVX2 指令集优化内存拷贝，提升 50% 性能
- **内存池**: 减少频繁的内存分配/释放
- **智能缓存**: 根据消息大小选择最优拷贝策略

### 自适应调优
- **动态线程池**: 根据 CPU/内存压力自动调整线程数
- **负载均衡**: 消息在多个处理线程间智能分发
- **背压控制**: 防止消息队列过载

## API 示例

### 注册消息处理器
```cpp
// 在 LogicSystem 中注册消息回调
callBackFunctions[1001] = std::bind(&LogicSystem::handleMessage, 
    this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
```

### 发送消息
```cpp
// 通过会话发送消息
session->send("Hello, Client!", 1001);
```

### 系统监控
```cpp
// 获取系统负载
double cpuUsage = AdvancedSystemMonitor::getInstance()->getCPUUsage();
double memPressure = AdvancedSystemMonitor::getInstance()->getMemoryPressure();
```

## 扩展开发

### 添加新的消息处理器
1. 在 `LogicSystem::registerCallBackFunction()` 中注册新的消息 ID
2. 实现对应的处理函数
3. 重新编译即可

### 自定义系统监控指标
1. 继承 `AdvancedSystemMonitor` 类
2. 实现特定的监控逻辑
3. 在配置中启用新的监控模块

## 许可证
本项目采用 MIT License 开源协议，详见 [LICENSE](LICENSE) 文件。

## 贡献指南
1. Fork 本项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

## 技术支持
- 提交 [Issue](../../issues) 报告 Bug 或请求新功能
- 查看 [Wiki](../../wiki) 获取详细文档
- 参与 [Discussions](../../discussions) 技术讨论

---

**AsioCoroutine** - 让异步网络编程更简单、更高效！ 🚀

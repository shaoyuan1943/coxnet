### coxnet
coxnet是轻量级、跨平台的Non-Block C++网络库。旨在提供简洁的API，利用操作系统高效的I/O模型实现高性能网络通讯。

* Windows： &#10004; 使用IOCP。
* Linux：   &#10004; 使用epoll搭配Edge-Triggered触发。
* macOS：   使用kqueue（WIP...）

### ✨ 功能特性
* **高性能IO模型**：底层自动选择平台最佳的IO模型，最大化网络吞吐量。
* **跨平台**：为Windows、Linux、macOS提供统一接口。
* **非同步事件驱动**：基于回调函数（Callback）式设计，使用时无需管理复杂的IO事件。
* **简洁的API**：核心API由Poller和Socket构成，大幅度减少接口暴露，简化使用。
* **自动缓冲区管理**：内建SimplBuffer自动调整大小，使用者无需关心缓冲区管理。
* **清晰的资源管理**：连接建立与关闭的优雅处理，内部负责资源释放，使用者无需关心资源处理。

### 📚 API

### 🛠️ 编译
coxnet实现为header-only方式，将coxnet代码目录引入到你的工程下，然后`#include "coxnet.h"`即可编译使用，具体使用方式参考`samples/`目下的client和server。

### 🚀 实现计划
1. `class Poller` for macOS
2. 提供接口形式的SimpleBuffer，去除coxnet层级的IO拷贝，进一步提升性能。
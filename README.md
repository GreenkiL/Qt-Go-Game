# Qt Online Go (Weiqi) Game Platform

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Qt](https://img.shields.io/badge/Qt-5.15+-green.svg)
![WebSocket](https://img.shields.io/badge/Protocol-WebSocket-blueviolet.svg)
![MySQL](https://img.shields.io/badge/Database-MySQL-orange.svg)

这是一个功能完善的、基于 C++ Qt 开发的网络围棋对战平台。项目包含独立的客户端和服务端，支持在线匹配对战和多种难度的人机对战。

## 🌟 主要功能

*   **完整的客户端/服务端架构**：基于 WebSocket 实现低延迟的实时通信。
*   **用户系统**：支持用户注册与登录，使用盐值哈希加密存储密码，保证账户安全。
*   **在线游戏大厅**：
    *   实时显示和刷新房间列表。
    *   支持创建房间、加入指定房间。
    *   实现自动匹配功能，快速开始对战。
*   **网络对战**：
    *   实现完整的围棋核心逻辑，包括落子、提子、禁入点（自杀）、打劫判断等规则。
    *   房间内实时聊天功能。
    *   支持游戏中认输、申请点目。
*   **单机对战 (人机模式)**：
    *   集成三种不同难度的 AI：
        1.  **初级 (Easy)**: 随机合法落子。
        2.  **中级 (Medium)**: 基于启发式算法（如评估吃子、做活、连接等）进行决策。
        3.  **高级 (Hard)**: 通过进程通信集成强大的开源围棋引擎 **KataGo**，提供接近职业水平的对弈体验。
*   **AI 形势判断**：在对局中，玩家可以随时请求 KataGo 引擎分析当前棋局的领地归属和胜率，并在棋盘上进行可视化展示。

## 📸 项目截图
| 登录与注册 | 游戏大厅 |
| :---: | :---: |
| ![登录界面](https://github.com/GreenkiL/Qt-Go-Game/blob/main/screenshots/login.png?raw=true) | ![游戏大厅](https://github.com/GreenkiL/Qt-Go-Game/blob/main/screenshots/lobby.png?raw=true) |
| **游戏对局** | **AI 形势判断** |
| ![游戏界面](https://github.com/GreenkiL/Qt-Go-Game/blob/main/screenshots/game.png?raw=true) | ![形势判断](https://github.com/GreenkiL/Qt-Go-Game/blob/main/screenshots/judge.png?raw=true) |
| **单机模式设置** | **服务端运行** |
| ![单机游戏](https://github.com/GreenkiL/Qt-Go-Game/blob/main/screenshots/singlegame.png?raw=true) | ![服务器](https://github.com/GreenkiL/Qt-Go-Game/blob/main/screenshots/server.png?raw=true) |
## 🛠️ 技术栈

*   **客户端 (Client)**:
    *   **语言**: C++17
    *   **框架**: Qt 5.15+ (Widgets)
    *   **网络**: `QWebSocket`
*   **服务端 (Server)**:
    *   **语言**: C++17
    *   **框架**: Qt 5.15+ (Core, Network)
    *   **网络**: `QWebSocketServer`
*   **数据库**: MySQL 8.0+
*   **AI 引擎**: KataGo (通过 `QProcess` 进行进程间通信)
*   **构建系统**: qmake

## 🏗️ 项目结构
*   `GameServer/`: 服务端核心逻辑，处理连接、房间管理、消息转发。
*   `AuthManager/`: 负责处理用户认证，与数据库交互。
*   `LoginWindow/`: 客户端登录/注册界面。
*   `LobbyWindow/`: 客户端游戏大厅界面。
*   `GameWindow/`: 核心游戏窗口，承载棋盘、对战逻辑。
*   `BoardWidget/`: 棋盘的UI渲染与用户交互。
*   `Goban/`: 围棋棋盘的核心数据结构与规则实现。
*   `SinglePlayerManager/`: 单机模式管理器，负责与AI算法或KataGo引擎交互。
*   `NetworkManager/`: 客户端网络连接与消息收发的封装。

## 🚀 如何构建与运行

#### 1. 环境依赖

*   **Qt**: 5.15 或更高版本
*   **C++ 编译器**: 支持 C++17 (MSVC, GCC, Clang)
*   **数据库**: MySQL 8.0+ 或 MariaDB
*   **AI 引擎 (可选)**: [KataGo](https://github.com/lightvector/KataGo/releases) (用于高级AI和形势判断)

#### 2. 数据库设置

1.  在你的 MySQL 服务器上创建一个新的数据库，例如 `go_game_db`。
2.  执行以下 SQL 语句创建 `users` 表：
    sql
    CREATE TABLE `users` (
      `id` int NOT NULL AUTO_INCREMENT,
      `username` varchar(255) NOT NULL,
      `password_hash` varchar(255) NOT NULL,
      `salt` varchar(255) NOT NULL,
      `nickname` varchar(255) DEFAULT NULL,
      `rating` int DEFAULT '1200',
      `wins` int DEFAULT '0',
      `losses` int DEFAULT '0',
      PRIMARY KEY (`id`),
      UNIQUE KEY `username` (`username`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

3.  修改 `GameServer/GameServer.cpp` 文件中的数据库连接信息。

#### 3. KataGo 引擎设置

1.  下载 KataGo 的 Windows/Linux 版本。
2.  在客户端可执行文件的同级目录下，创建一个名为 `katago` 的文件夹。
3.  将 KataGo 的可执行文件 (`katago.exe`)、模型文件 (`.bin.gz`) 和配置文件 (`gtp_config.cfg`) 放入 `katago` 文件夹。

#### 4. 编译

1.  使用 Qt Creator 打开项目根目录下的 `.pro` 文件。
2.  分别配置并构建 `Client` 和 `Server` 子项目。

#### 5. 运行

1.  确保你的 MySQL 服务正在运行。
2.  首先启动 `Server` 程序。
3.  然后启动一个或多个 `Client` 程序进行游戏。

# AsulDownloader

Qt 高性能下载库 - 基于 AsulMultiDownloader 实现的 Minecraft 资源下载器

## 最新更新 ✨

### 停滞连接检测和自动重试 (Stall Detection & Auto-Retry)

解决了脏连接（0.00MB/s）和下载停滞问题：
- ✅ 自动检测和清理无进度的连接
- ✅ 15秒无数据传输自动重试
- ✅ 无需手动重启程序
- ✅ 提升下载成功率和速度

详细说明：[STALL_DETECTION_IMPROVEMENTS.md](STALL_DETECTION_IMPROVEMENTS.md)  
快速开始：[QUICK_START_STALL_DETECTION.md](QUICK_START_STALL_DETECTION.md)

## 功能特性

AsulDownloader 是一个高性能的 Minecraft 资源下载工具，能够：

- 📦 **解析并下载 assets.json 中的所有资源文件**
  - 自动创建正确的目录结构
  - 使用哈希值构建下载 URL 和本地路径
  - 智能跳过已下载的文件

- 🎮 **解析并下载 version.json 中的文件**
  - 下载 client.jar 并重命名为 `{version}.jar`
  - 下载所有 libraries 到正确的目录结构

- ⚡ **高性能并发下载**
  - 默认 512 线程并发下载
  - 智能调度，充分利用网络带宽
  - 大文件自动分段下载（>10MB）
  - 实时进度统计和速度监控

- 🔧 **智能连接管理** (新增)
  - 自动检测停滞连接（0.00MB/s）
  - 15秒无进度自动重试
  - 最多3次自动重试
  - 脏连接自动清理

## 目录结构

下载的文件将保存到以下目录结构：

```
./minecraft/
├── assets/
│   └── objects/
│       ├── 00/
│       │   └── {hash}
│       ├── 01/
│       │   └── {hash}
│       └── ...
├── versions/
│   └── {version}/
│       └── {version}.jar
└── libraries/
    └── {path from version.json}
```

## 构建和运行

### 环境要求

- Qt 6.7.3 或更高版本
- CMake 3.16 或更高版本
- C++17 编译器

### 构建步骤

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### 运行

将 `assets.json` 和 `version.json` 放在可执行文件所在目录，然后运行：

```bash
./AsulDownloader
```

## 使用说明

1. 准备 JSON 文件：
   - `assets.json` - Minecraft 资源索引文件
   - `version.json` - Minecraft 版本配置文件

2. 运行 AsulDownloader

3. 程序将自动：
   - 解析 JSON 文件
   - 创建必要的目录结构
   - 开始并发下载所有文件
   - 显示实时进度和统计信息

4. 下载完成后，所有文件将保存在 `./minecraft/` 目录下

## 输出示例

```
AsulDownloader - Minecraft Asset and Version Downloader
========================================================

Parsing assets.json...
Found 4585 asset objects
Added 4585 new asset download tasks

Parsing version.json...
Version: 1.21.11
Added client.jar download: 1.21.11
Found 107 libraries
Added 95 new library download tasks

Total tasks added: 4680
Starting downloads...

[STATS] Active: 16 | Queued: 4664 | Completed: 0 | Failed: 0 | Speed: 2048 KB/s
[100/4680] Completed: b62ca8ec10d07e6bf5ac8dae0c8c1d2e6a1e3356
[200/4680] Completed: 5ff04807c356f1beed0b86ccf659b44b9983e3fa
...

========================================================
All downloads finished!
Completed: 4680, Failed: 0, Total: 4680
========================================================
```

## 技术架构

基于 AsulMultiDownloader 高性能下载库，具有：

- 智能线程调度
- 断点续传支持
- 动态速度监控
- Host 连接管理
- 自动重试机制

详细技术文档请参考 [Techni.md](Techni.md)

## 许可证

本项目遵循相应的开源许可证。

# sinmai-asio

为 Sinmai 提供原生 ASIO 音频输出的独立 MelonLoader 插件。

插件接管 CRI 的输出后端，由 ASIO 硬件时钟驱动音频渲染，减少输出链路中的缓冲等待。音频处理在原生线程中完成，不依赖 Unity 每帧搬运 PCM，也不修改磁盘上的游戏 DLL。

## 兼容范围

| 项目 | 当前支持 / 已验证 |
| --- | --- |
| 平台 | Windows x64 |
| 游戏 | Sinmai DX 1.70 的指定程序集与 CRI DLL |
| 加载器 | MelonLoader 0.6.4，Mono 游戏 |
| 声卡 | Steinberg UR22C，Yamaha Steinberg USB ASIO |
| 输出 | 48 kHz、双声道 |
| 缓冲 | 32 / 64 / 128 samples，默认 128 |

当前版本使用固定的 DLL 指纹和原生接口布局。**版本号相同也不代表二进制兼容**；不匹配的程序集或 CRI DLL 会被拒绝加载。已支持的指纹记录在 [dependencies.json](dependencies.json)。其他游戏版本、声卡与驱动尚需单独验证。

UR22C 建议从 **128 samples / Low Latency** 开始。64 samples 在验证环境中出现过欠载，不保证更小缓冲能够稳定运行。缓冲大小只是延迟的一部分，不能直接视为实际出声延迟。

本插件不依赖 AquaMai 或 MuMod。与 AquaMai 同时使用时，其音量和声道路由设置仍在输出前生效；最终输出由本插件接管。

## 安装

先安装声卡厂商提供的 **64 位原生 ASIO 驱动**，并关闭游戏。

使用发布的安装包，或先按下文从源码构建。在项目 / 安装包目录运行：

```powershell
.\install.ps1 -GameRoot "D:\Games\Sinmai\Package"
```

脚本会安装插件，保留已有的 `SinmaiAsio.toml`，并备份被替换的插件文件。没有配置文件时，会从示例创建一份，初始 `Enabled = false`。

也可以手动放置文件：

```text
游戏目录/
├── Mods/
│   └── SinmaiAsio.dll
├── UserLibs/
│   └── SinmaiAsio/
│       └── SinmaiAsio.Native.dll
└── SinmaiAsio.toml
```

手动安装时，将 `build/` 中的两个 DLL 放入对应位置，将 `SinmaiAsio.example.toml` 复制为游戏目录下的 `SinmaiAsio.toml`。升级时保留自己的配置。

启动游戏前：

1. 在 ASIO 驱动面板中设置 **48 kHz** 和所需缓冲大小。UR22C 建议使用 **128 samples / Low Latency**。
2. 在 `SinmaiAsio.toml` 中设置相同的 `BufferSamples`，并将 `Enabled` 改为 `true`。
3. 启动游戏。正常情况下，日志会输出一条 ASIO 启动信息。

驱动面板与配置的缓冲大小必须一致；插件不会自动修改驱动设置或增大缓冲。配置修改后需重启游戏。

## 配置

```toml
Enabled = true
Driver = "Yamaha Steinberg USB ASIO"
BufferSamples = 128
LeftChannel = 0
RightChannel = 1
Diagnostics = false
```

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `Enabled` | `false` | 启用 ASIO 输出 |
| `Driver` | `"Yamaha Steinberg USB ASIO"` | 已安装的 64 位 ASIO 驱动注册名称，不是 Windows 播放设备名称 |
| `BufferSamples` | `128` | 缓冲大小，可选 `32`、`64`、`128`；须与驱动面板一致 |
| `LeftChannel` | `0` | 左声道输出通道，从 0 开始编号 |
| `RightChannel` | `1` | 右声道输出通道，从 0 开始编号，不能与左声道相同 |
| `Diagnostics` | `false` | 开启详细日志和统计文件，仅排查问题时使用 |

采样率固定为 48 kHz，不进行实时重采样。声卡输出格式由驱动报告，不支持的格式会明确报错。

### 欠载与错误处理

偶发欠载会对缺失的音频样本补零，并继续输出。启动后的第一秒为预热期；此后若 10 秒内累计出现 3 次欠载回调，插件会停止音频并显示错误。

驱动重置、采样率变化、回调中断或后端冲突也会停止输出。**不会自动重试、增大缓冲或回退到 WASAPI**，排除问题后需重启游戏。

若小缓冲频繁欠载，可在驱动面板和配置中同时改用 128 samples。更低的驱动报告延迟，不一定意味着更稳定的游戏输出。

## 排查问题

正常运行只记录启动信息和错误，不定时刷屏。

需要详细统计时，将 `Diagnostics = true` 后重启游戏：

- 日志：`MelonLoader/Latest.log`
- 统计：`UserData/SinmaiAsio.stats.json`，每 5 秒更新一次

关闭诊断后不会继续更新统计文件；已有文件可能是上一次诊断的记录。

可以汇总统计文件：

```powershell
.\tools\summarize-stats.ps1 -Path "D:\Games\Sinmai\Package\UserData\SinmaiAsio.stats.json"
```

统计中的渲染等待时间、回调耗时和驱动报告延迟是不同的指标，均不能替代物理或声学端到端测量。

反馈问题时，请提供游戏版本、插件版本、声卡 / 驱动版本、缓冲设置以及相关错误日志。请勿提交游戏程序集、音频资源或其他游戏文件。

## 从源码构建

需要：

- PowerShell
- Visual Studio C++ x64 构建工具及 Windows SDK
- .NET Framework 4.7.2 Targeting Pack
- 本地游戏与 MelonLoader 文件，用作编译引用

```powershell
.\build.ps1 -GameRoot "D:\Games\Sinmai\Package" -FetchDependencies
```

首次构建会下载固定版本的 ASIO SDK 和 MinHook，校验 SHA256 后解包。版本、来源和校验值见 [dependencies.json](dependencies.json)。构建产物位于 `build/`；构建流程同时运行音频转换 / 缓冲测试及 CRI 指纹 / Hook 撤销测试，不会启动游戏。

独立驱动探测工具：

```powershell
# 检查驱动、声道、格式和缓冲支持
.\build\asio-probe.exe --buffer 128

# 运行 10 秒静音输出
.\build\asio-probe.exe --buffer 128 --silence 10

# 播放 3 秒低音量 440 Hz 测试音
.\build\asio-probe.exe --buffer 128 --tone 3
```

生成包含二进制、源码和许可证的安装包：

```powershell
.\package.ps1
```

进一步的开发资料：

- [原生适配层与线程模型](docs/architecture.md)
- [验证记录与未覆盖项目](docs/validation.md)
- `tests/`：原生测试
- `tools/run-probe.ps1`：开发用 capture / clock 探针，普通启动无需运行

## 卸载

关闭游戏后删除：

- `Mods/SinmaiAsio.dll`
- `UserLibs/SinmaiAsio/SinmaiAsio.Native.dll`

`SinmaiAsio.toml` 和统计文件可自行保留或删除。下次启动时，游戏恢复使用原有音频输出路径。

## 许可证与依赖

本项目采用 [GPL-3.0-only](LICENSE)。

- ASIO SDK：选择 GPLv3 许可选项使用。
- MinHook：BSD-2-Clause。
- MelonLoader、Harmony、Tomlet 及 Unity 程序集由本地游戏环境提供，不随本项目分发。

安装包保留相关依赖的许可证。ASIO 名称与标识的使用仍须遵守 Steinberg 的商标规范。本项目不包含游戏文件或声卡驱动。

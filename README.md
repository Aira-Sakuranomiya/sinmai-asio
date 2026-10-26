# sinmai-asio

为 Sinmai 提供原生 ASIO 音频输出的独立 MelonLoader 插件。

插件接管 CRI 的输出后端，由 ASIO 硬件时钟驱动音频渲染，减少输出链路中的缓冲等待。音频处理在原生线程中完成，不依赖 Unity 每帧搬运 PCM，也不修改磁盘上的游戏 DLL。

## 兼容范围

| 项目 | 当前支持 / 已验证 |
| --- | --- |
| 平台 | Windows x64 |
| 游戏 | Sinmai DX 1.70 的指定程序集与 CRI DLL |
| 加载器 | MelonLoader 0.6.4，Mono 游戏 |
| 已验证声卡 | Yamaha URX22C，Yamaha Steinberg USB ASIO |
| 输出 | 48 kHz、双声道 |
| 缓冲 | 32 / 64 / 128 samples，默认 128 |

当前版本使用固定的 DLL 指纹和原生接口布局。**版本号相同也不代表二进制兼容**；不匹配的程序集或 CRI DLL 会被拒绝加载。已支持的指纹记录在 [dependencies.json](dependencies.json)。其他游戏版本、声卡与驱动尚需单独验证。

插件不按声卡型号分别编译。**同一份 Windows x64 DLL 可以通过配置选择其他 ASIO 驱动，无需为换声卡重新编译。** 目前只对 URX22C 做过实机验证，其他设备能否使用取决于驱动的采样率、缓冲和输出格式支持。

URX22C 建议从 **128 samples / Low Latency** 开始。64 samples 在验证环境中出现过欠载，不保证更小缓冲能够稳定运行。缓冲大小只是延迟的一部分，不能直接视为实际出声延迟。

本插件不依赖 AquaMai 或 MuMod。与 AquaMai 同时使用时，其音量和声道路由设置仍在输出前生效；最终输出由本插件接管。

## 安装

运行游戏的电脑需要装好 MelonLoader，以及声卡厂商提供的 **64 位原生 ASIO 驱动**。插件可以使用编译好的文件，也可以从源码自行编译；两种方式最终安装的 DLL 相同。

### 方式一：使用编译好的文件

下载并解压发布包，取出以下两个文件即可，无需安装 Visual Studio、Windows SDK 或 ASIO SDK：

- `build/SinmaiAsio.dll`
- `build/SinmaiAsio.Native.dll`

随后按下文“复制到游戏目录”放置文件并配置。

GitHub 自动生成的 **Source code** 压缩包只有源码，不包含编译好的 DLL；使用它需要按方式二自行编译。

### 方式二：从源码编译

编译电脑需要 PowerShell、Visual Studio C++ x64 构建工具、Windows SDK、.NET Framework 4.7.2 Targeting Pack，以及用于编译引用和校验的游戏 / MelonLoader 依赖文件。**仅编译不需要连接声卡或安装 ASIO 驱动。**

下载或克隆源码，在项目根目录执行以下命令，将示例路径替换为实际游戏目录：

```powershell
.\build.ps1 -GameRoot "D:\Games\Sinmai\Package" -FetchDependencies
```

首次构建会下载并校验 ASIO SDK 和 MinHook。构建成功后，安装时同样只需取出 `build/SinmaiAsio.dll` 和 `build/SinmaiAsio.Native.dll`。`build/` 只保留这两个运行文件；编译中间文件、测试程序和驱动探测工具位于 `.build/`，不需要安装。

更详细的构建依赖和开发工具见[从源码构建](#从源码构建)。

### 复制到游戏目录

关闭游戏，将两个 DLL 按以下目录放置；缺少的文件夹需自行创建。升级前可备份旧 DLL，并保留已有配置。

```text
游戏目录/
├── Mods/
│   └── SinmaiAsio.dll
├── UserLibs/
│   └── SinmaiAsio/
│       └── SinmaiAsio.Native.dll
└── SinmaiAsio.toml
```

**首次加载会自动在游戏目录生成 `SinmaiAsio.toml`，只复制两个 DLL 即可。** 已有配置不会被覆盖，也不会重新选择驱动。

首次自动选择规则：

- 存在 `Yamaha Steinberg USB ASIO`：优先选择并启用。
- 没有上述驱动，但只有一个已注册的 64 位 ASIO 驱动：选择并启用该驱动。
- 没有驱动，或存在多个其他候选：生成禁用配置并记录提示，需手动填写 `Driver` 并启用。

默认使用 48 kHz、128 samples、输出通道 0 / 1。驱动面板仍需预先设置为相同采样率和缓冲大小；自动选择驱动不会修改驱动面板。不匹配时会明确报错。需要预先配置时，可复制 `SinmaiAsio.example.toml` 并重命名，也可按照下文手动创建。

### 配置并启用

1. 在 ASIO 驱动面板中设置 **48 kHz** 和所需缓冲大小。URX22C 建议使用 **128 samples / Low Latency**。
2. 自动选择到正确驱动且使用默认设置时，无需手动创建配置。需要自定义时，编辑 `SinmaiAsio.toml`，设置 `Driver`、`BufferSamples` 和输出通道，确保 `Enabled = true`。
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
| `Enabled` | 自动选择到驱动时为 `true` | 启用 ASIO 输出；未找到唯一候选时为 `false` |
| `Driver` | 首次加载时自动选择 | 已安装的 64 位 ASIO 驱动注册名称，不是 Windows 播放设备名称 |
| `BufferSamples` | `128` | 缓冲大小，可选 `32`、`64`、`128`；须与驱动面板一致 |
| `LeftChannel` | `0` | 左声道输出通道，从 0 开始编号 |
| `RightChannel` | `1` | 右声道输出通道，从 0 开始编号，不能与左声道相同 |
| `Diagnostics` | `false` | 开启详细日志和统计文件，仅排查问题时使用 |

采样率固定为 48 kHz，不进行实时重采样。目前支持小端 16 / 24 / 32 位整数 PCM，以及 32 / 64 位浮点格式；部分带有效位对齐的整数格式、DSD 和大端格式尚不支持。声卡格式由驱动报告，不支持的格式会明确报错。

### 更换声卡

在运行游戏的电脑上安装目标声卡的 64 位 ASIO 驱动，然后修改 `Driver`、输出通道及缓冲设置即可，不需要重新编译插件。

可以在 **64 位 PowerShell** 中查看已注册的 ASIO 驱动名称：

```powershell
Get-ChildItem 'HKLM:\SOFTWARE\ASIO' | Select-Object -ExpandProperty PSChildName
```

将目标名称填入 `Driver`，在驱动面板中设置 48 kHz，并选择驱动支持的 `32`、`64` 或 `128` samples。`BufferSamples` 必须与之匹配，`LeftChannel` / `RightChannel` 必须是有效且不同的输出通道。

更换声卡不会改变游戏版本限制：游戏程序集和 CRI DLL 仍须匹配当前支持的指纹。

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

**编译不需要连接声卡或安装 ASIO 驱动。** ASIO SDK 是编译依赖，声卡驱动是运行依赖。仅运行 `asio-probe.exe` 或在游戏中实际启用输出时，才需要目标声卡及其驱动。

构建机器需要：

- PowerShell
- Visual Studio C++ x64 构建工具及 Windows SDK
- .NET Framework 4.7.2 Targeting Pack
- 受支持的 CRI DLL，以及 Unity / MelonLoader 依赖文件，用于编译引用和本地接口校验

```powershell
.\build.ps1 -GameRoot "D:\Games\Sinmai\Package" -FetchDependencies
```

首次构建会下载固定版本的 ASIO SDK 和 MinHook，校验 SHA256 后解包。版本、来源和校验值见 [dependencies.json](dependencies.json)。`build/` 仅包含两个运行 DLL；`.build/` 存放编译中间文件、测试程序和探测工具。构建流程同时运行音频转换 / 缓冲、CRI 指纹 / Hook 撤销及首次配置生成测试，不会启动游戏。

独立驱动探测工具：

```powershell
# 检查驱动、声道、格式和缓冲支持
.\.build\asio-probe.exe --buffer 128

# 运行 10 秒静音输出
.\.build\asio-probe.exe --buffer 128 --silence 10

# 播放 3 秒低音量 440 Hz 测试音
.\.build\asio-probe.exe --buffer 128 --tone 3
```

生成包含二进制、源码、配置示例和许可证的发布包：

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

发布包保留相关依赖的许可证。ASIO 名称与标识的使用仍须遵守 Steinberg 的商标规范。本项目不包含游戏文件或声卡驱动。

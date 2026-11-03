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
| 缓冲 | 32 / 48 / 64 / 96 / 128 / 192 / 256 / 384 / 512 / 768 / 1024 / 1536 / 2048 samples，默认自动跟随驱动 |

当前版本使用固定的 DLL 指纹和原生接口布局。**版本号相同也不代表二进制兼容**；不匹配的程序集或 CRI DLL 会被拒绝加载。已支持的指纹记录在 [dependencies.json](dependencies.json)。其他游戏版本、声卡与驱动尚需单独验证。

插件不按声卡型号分别编译。**同一份 Windows x64 DLL 可以通过配置选择其他 ASIO 驱动，无需为换声卡重新编译。** 目前只对 URX22C 做过实机验证，其他设备能否使用取决于驱动的采样率、缓冲和输出格式支持。

URX22C 建议从 **128 samples / Low Latency** 开始。64 samples 在验证环境中出现过欠载，不保证更小缓冲能够稳定运行。缓冲大小只是延迟的一部分，不能直接视为实际出声延迟。新增的 48 / 96 / 192 / 256 / 384 / 512 / 768 / 1024 / 1536 / 2048 samples 尚未实机验证；能否使用仍取决于驱动报告的缓冲范围和步进约束。

本插件不依赖 AquaMai 或 MuMod。与 AquaMai 同时使用时，其音量和声道路由设置仍在输出前生效；最终输出由本插件接管。

## 安装

运行游戏的电脑需要装好 MelonLoader，以及声卡厂商提供的 **64 位原生 ASIO 驱动**。插件可以使用编译好的文件，也可以从源码自行编译；两种方式最终安装的 DLL 相同。

### 方式一：使用编译好的文件

下载并解压发布包，取出以下两个文件即可，无需安装 Visual Studio、Windows SDK 或 ASIO SDK：

- `build/Mods/SinmaiAsio.dll`
- `build/UserLibs/SinmaiAsio/SinmaiAsio.Native.dll`

随后按下文“复制到游戏目录”放置文件并配置。

GitHub 自动生成的 **Source code** 压缩包只有源码，不包含编译好的 DLL；使用它需要按方式二自行编译。

### 方式二：从源码编译

编译电脑需要 PowerShell、Visual Studio C++ x64 构建工具、Windows SDK、.NET Framework 4.7.2 Targeting Pack，以及用于编译引用和校验的游戏 / MelonLoader 依赖文件。**仅编译不需要连接声卡或安装 ASIO 驱动。**

下载或克隆源码，在项目根目录执行以下命令，将示例路径替换为实际游戏目录：

```powershell
.\build.ps1 -GameRoot "D:\Games\Sinmai\Package" -FetchDependencies
```

首次构建会下载并校验 ASIO SDK 和 MinHook。构建成功后，直接将 `build/` 中的 `Mods/` 和 `UserLibs/` 两个文件夹复制到游戏目录并合并即可。两个运行 DLL 已按游戏目录结构放置；编译中间文件、测试程序和驱动探测工具位于 `.build/`，不需要安装。

更详细的构建依赖和开发工具见[从源码构建](#从源码构建)。

升级到 0.2.0 时请同时替换两个 DLL；原生接口已升级至 ABI 6，不可混用旧版文件。

### 复制到游戏目录

关闭游戏，将 `build/` 中的 `Mods/` 和 `UserLibs/` 两个文件夹复制到游戏目录，合并文件夹并替换对应插件 DLL 即可。升级前可备份旧 DLL，并保留已有配置。

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

默认使用 48 kHz、输出通道 0 / 1，`BufferSamples = 0` 表示启动时读取 ASIO 驱动报告的首选缓冲大小，并自动匹配 CRI 渲染。只需在驱动面板选择缓冲，无需在 TOML 中重复填写。已有配置不会自动改写；从旧版升级时，将 `BufferSamples` 改成 `0` 即可启用自动模式。需要预先配置时，可复制 `SinmaiAsio.example.toml` 并重命名，也可按照下文手动创建。

### 配置并启用

1. 在 ASIO 驱动面板中设置 **48 kHz** 和所需缓冲大小。URX22C 建议使用 **128 samples / Low Latency**。
2. 自动选择到正确驱动且使用默认设置时，无需手动创建配置。需要自定义时，编辑 `SinmaiAsio.toml`，设置 `Driver` 和输出通道，确保 `Enabled = true`；保留 `BufferSamples = 0` 即可自动跟随驱动。
3. 启动游戏。正常情况下，日志会输出一条 ASIO 启动信息。

自动模式使用驱动报告的首选缓冲，插件不会在运行中自动增大缓冲。手动指定正整数时，仍需确保驱动支持该值。驱动面板或配置修改后需重启游戏；首版不支持运行中热切换。采样率仍固定为 48 kHz，不随驱动改用其他采样率。

## 配置

```toml
Enabled = true
Driver = "Yamaha Steinberg USB ASIO"
BufferSamples = 0
LeftChannel = 0
RightChannel = 1
Diagnostics = false
CaptureOutput = false
```

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `Enabled` | 自动选择到驱动时为 `true` | 启用 ASIO 输出；未找到唯一候选时为 `false` |
| `Driver` | 首次加载时自动选择 | 已安装的 64 位 ASIO 驱动注册名称，不是 Windows 播放设备名称 |
| `BufferSamples` | `0` | `0` 自动使用驱动首选缓冲；手动可选 `32`、`48`、`64`、`96`、`128`、`192`、`256`、`384`、`512`、`768`、`1024`、`1536`、`2048`；须由驱动支持 |
| `LeftChannel` | `0` | 左声道输出通道，从 0 开始编号 |
| `RightChannel` | `1` | 右声道输出通道，从 0 开始编号，不能与左声道相同 |
| `Diagnostics` | `false` | 开启详细日志和统计文件，仅排查问题时使用 |
| `CaptureOutput` | `false` | 诊断录音：在内存保留最近最多 30 秒 ASIO 输出，收到请求后才保存 WAV |

采样率固定为 48 kHz，不进行实时重采样。目前支持小端 16 / 24 / 32 位整数 PCM，以及 32 / 64 位浮点格式；部分带有效位对齐的整数格式、DSD 和大端格式尚不支持。声卡格式由驱动报告，不支持的格式会明确报错。

### 更换声卡

在运行游戏的电脑上安装目标声卡的 64 位 ASIO 驱动，然后修改 `Driver`、输出通道及缓冲设置即可，不需要重新编译插件。

可以在 **64 位 PowerShell** 中查看已注册的 ASIO 驱动名称：

```powershell
Get-ChildItem 'HKLM:\SOFTWARE\ASIO' | Select-Object -ExpandProperty PSChildName
```

将目标名称填入 `Driver`，在驱动面板中设置 48 kHz，并选择驱动支持的 `32`、`48`、`64`、`96`、`128`、`192`、`256`、`384`、`512`、`768`、`1024`、`1536` 或 `2048` samples。`BufferSamples = 0` 时会自动跟随启动时驱动报告的首选值；手动指定时需匹配驱动设置。`LeftChannel` / `RightChannel` 必须是有效且不同的输出通道。

更换声卡不会改变游戏版本限制：游戏程序集和 CRI DLL 仍须匹配当前支持的指纹。

### CRI 内部渲染量子（诊断版本）

0.2.0 保持声卡缓冲和 CRI 处理频率不变，只把每次内部混音调用的最大量子由 256 改为 32 samples。原有循环仍按 ASIO 需要的采样数填满输出块，不增加播放队列或第二个渲染时钟；代价是内部混音调用更频繁。

此版本用于验证录音中反复出现的块尾静音是否与混音块大小有关，效果尚需实机确认。修改只在已校验 DLL 的内存中进行，先核对指令签名，退出后恢复原始字节，不修改磁盘上的游戏 DLL。

### 输出峰值保护

输出转换前启用立体声联动软限幅：两声道峰值不超过 0.90 时保持原样，超过后以平滑曲线压缩，输出峰值上限约为 0.98。左右声道使用相同增益，以保留声道比例；NaN / Infinity 样本替换为零。

该处理不预读、不积攒音频、不增加缓冲块，只增加逐样本计算。它会改变高电平峰值的动态和音色，严重过载时仍可能失真；不能修复欠载、驱动爆音或音源中已经存在的失真。

开启 `Diagnostics` 后，统计文件额外记录以下累计值，不增加逐次日志：

| 字段 | 含义 |
| --- | --- |
| `inputPeak` | 限幅前有限样本的最大绝对值；1.0 对应数字满幅，异常值不计入此峰值 |
| `overRangeFrames` | 任一声道绝对值超过 1.0 的立体声帧数 |
| `limitedFrames` | 触发软限幅的立体声帧数；包含未超过 1.0 但进入软拐点的帧 |
| `invalidSamples` | 被替换为零的 NaN / Infinity 样本数，按声道分别计数 |

### 欠载与错误处理

欠载时仅对当前缺失的音频样本补零，保持 ASIO 回调和 CRI 渲染运行；音频数据到达后自动恢复播放，不会因欠载次数达到阈值而停止输出。欠载仍会计入统计，但不会逐次写日志。启动后第一秒的欠载单独作为预热统计。

驱动重置、采样率变化、回调中断或后端冲突仍会停止输出，这些故障需排除问题后重启游戏。插件不会自动增大缓冲或回退到 WASAPI。

若小缓冲频繁欠载，可在驱动面板改用 128 samples 或更大缓冲后重启游戏。自动模式无需修改 TOML；手动模式需同步修改 `BufferSamples`。更低的驱动报告延迟，不一定意味着更稳定的游戏输出。

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

### 诊断录音

需要区分游戏内 PCM 异常和驱动 / 设备侧爆音时，将 `CaptureOutput = true` 后重启游戏。建议同时启用 `Diagnostics`。该开关默认关闭；开启后，内存中滚动保留最近最多 30 秒实际写入 ASIO 驱动双缓冲的两声道 PCM，包含软限幅、欠载补零和格式转换结果。当前 Int32 双声道记录区合计约 22 MiB，其他格式最多约 44 MiB。

听到爆音后，保持游戏运行，执行：

```powershell
.\tools\save-capture.ps1 -GameRoot "D:\Games\Sinmai\Package"
```

也可以在游戏的 `UserData/` 下创建一个名为 `SinmaiAsio.capture` 的空文件。插件每 5 秒检查一次，处理后删除请求文件；后台线程将片段保存到：

- `UserData/SinmaiAsio-Capture-时间戳-标识.wav`：48 kHz、双声道、32 位浮点 WAV；按驱动格式解码，用于听感 / 波形对照。
- 同名 `.wav.stats.json`：如果诊断已启用，附上请求时最近一次统计，时间可能相差一个统计周期。

保存时切换两个预分配记录区，音频回调只复制已编码的缓冲内容，不等待磁盘；文件写入和解码在后台线程进行。每次保存后，新记录区重新积累，因此短时间内再次保存可能不足 30 秒。录音错误只记录提示，不会停止播放。退出游戏不会自动保存，须在游戏运行时触发。

这不是声卡回录：文件干净也不能证明实际输出没有爆音，它只能帮助划分问题发生在驱动提交前还是提交后。录音文件可能包含游戏音乐，请仅用于本机排查，不随发布包或问题报告公开上传。排查结束后将 `CaptureOutput` 改回 `false` 并重启游戏。

### 输出时序诊断

开启 `Diagnostics` 后，同一统计文件还包含以下累计指标；仍然每 5 秒聚合输出，不逐回调写日志：

| 字段 | 含义 |
| --- | --- |
| `callbackMaxGapUs` / `callbackMaxGapExcessUs` | ASIO 回调入口的最大间隔，以及间隔超出理论周期的最大值，单位 μs；与回调内部执行耗时不同 |
| `callbackGapCount` | 回调间隔比较次数；首个回调不参与比较，统计包含启动阶段 |
| `callbackGapsOver150Percent` / `callbackGapsOver200Percent` | 间隔超过理论周期 1.5 / 2 倍的次数；属于时序异常线索，不是已确认的声卡欠载次数 |
| `timeInfoCallbacks` / `legacyCallbacks` | 带 ASIOTime 数据 / 传统无时间信息回调的次数 |
| `validPositionCallbacks` / `unavailablePositionCallbacks` | 驱动提供有效采样位置 / 未提供有效位置的回调次数；位置不可用不能视为连续性正常 |
| `positionDiscontinuities` | 相邻有效回调的采样位置推进量不等于一个缓冲块的次数 |
| `positionForwardGapSamples` | 采样位置向前推进超出一个缓冲块的累计样本数；不是物理丢样的实测值 |
| `positionBackwardSteps` / `positionRepeatedSteps` / `positionShortSteps` | 采样位置倒退、重复或推进不足一个缓冲块的次数 |
| `resyncRequests` / `resetRequests` | 驱动实际发出的重同步 / 重置请求数 |
| `bufferChangeRequests` / `latencyChangeRequests` | 驱动发出的缓冲大小变化 / 延迟变化通知数 |

例如 256 samples / 48 kHz 的理论回调周期约为 5333 μs。即使 `missingAfterWarmup = 0`，仍可能出现回调来晚；`maxCallbackUs` 只统计回调入口之后的工作时间。相反，间隔或位置异常也不能单独证明发生了可听见的断音，需要与复现时间、驱动跟踪或回录结果对照。未提供有效采样位置时，插件不会额外查询驱动。驱动事件仍遵循原有的报错停止策略，此版本只增加诊断。

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

首次构建会下载固定版本的 ASIO SDK 和 MinHook，校验 SHA256 后解包。版本、来源和校验值见 [dependencies.json](dependencies.json)。`build/Mods/` 与 `build/UserLibs/SinmaiAsio/` 分别存放托管和原生 DLL，可直接按目录复制到游戏中；`.build/` 存放编译中间文件、测试程序和探测工具。构建流程同时运行音频转换 / 缓冲、CRI 指纹 / Hook 撤销及首次配置生成测试，不会启动游戏。

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

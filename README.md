# EPD：会播放音乐的 CD 冰箱贴（MP3 播放验证工程）

> **EPD = Electronic Paper Display（电子墨水屏）**。工程名取自最终产品要接的那块屏幕，而不是当前已实现的功能——本仓库目前是**播放链路验证工程**：先把「SD 卡里的 MP3 能流畅播出来」跑通；最终形态是一枚能播放音乐的冰箱贴，正面用电子墨水屏显示 CD 封面，相当于一个怀旧的 CD 盒。
>
> IDE：Keil MDK（UV4/UV5，工程 `USER/EPD.uvprojx`）　|　目标芯片：STM32F103ZE（Cortex-M3 @72MHz，512KB Flash / 64KB 内部 SRAM）　|　仓库：`https://github.com/Aaron-XSJ/EPD`

---

## 一、简介（可直接引用）

本工程是一套基于 **STM32F103 核心板** 的 MP3 播放器验证程序：音频文件存放在 **TF 卡（microSD）** 中，MCU 通过 **SDIO 4 位总线** 读卡，借助 **FatFs R0.11** 文件系统完成挂载、目录扫描与文件读取；解码侧通过 **SPI1** 外接 **VS1003** 音频解码芯片，由硬件完成 MP3/WAV 解码与模拟输出。

播放链路的核心是 **双缓冲（乒乓缓冲）+ DREQ 流控**：两块 512 字节缓冲区交替承担「从 SD 卡预读」和「向 VS1003 推送数据」，程序只在 DREQ 为低（芯片 FIFO 已满、正在解码）时执行阻塞式 SD 读，把读卡耗时隐藏在解码时间里；DREQ 变高后再以 32 字节为单元喂数据。这样在 STM32F103 这种无操作系统、无 DMA 参与的简单裸机环境下，也能保证数据流不断线，播放流畅、不卡顿。

工程还实现了 **根目录音频自动扫描、列表循环播放、按键切歌 / 暂停、LED 故障提示**，并保留了正弦测试、RAM 测试、码率/解码时间读取等 VS10XX 调试接口。它是后续接电子墨水屏、做成 CD 封面冰箱贴的播放底座，产品定位与待办见第二节。

---

## 二、项目背景与产品定位

### 名字的由来

工程与固件都叫 **EPD**，取自 **Electronic Paper Display（电子墨水屏）**——因为这块屏是产品立项时就定下的核心部件。当前代码里**还没有**任何墨水屏驱动，`HARDWARE/LCD` 是开发板模板自带的 TFT LCD 代码，与 EPD 无关。

### 最终产品

一枚**可以播放音乐的冰箱贴**：贴在冰箱上，正面是电子墨水屏，显示当前播放曲目的 **CD 封面**，配上曲名 / 专辑 / 播放进度；按下按键切歌、暂停，音乐从内置小喇叭出来。整体尺寸与视觉按 **CD 盒（正方形封面 + 窄边框）** 来做，是一种怀旧实物化的音乐陈列：不点亮时封面仍留在屏上，像一张摆在冰箱门上的 CD 封套。

**为什么选电子墨水屏**：断电保图、静态显示几乎不耗电，适合长期挂着的冰箱贴；类纸质感接近印刷封面，没有背光、不刺眼；只在切歌或换专辑时刷新一次，反而强化了「实体唱片」的静态仪式感，而不是做一块常亮的小电视。

### 当前阶段 vs 目标形态

| 维度 | 当前验证工程（本仓库） | 目标产品 |
| --- | --- | --- |
| 播放链路 | STM32F103ZE + VS1003，双缓冲已跑通 | 沿用，做小型化与低功耗优化 |
| 曲库 | TF 卡根目录、8.3 短名、最多 32 首 | 按专辑组织，曲库 + 封面 + 元数据 |
| 显示 | **无**（TFT LCD 模板未参与播放） | 电子墨水屏显示 CD 封面、曲名、进度 |
| 交互 | WK_UP / KEY0 两按键 + LED 提示 | 切歌 / 暂停 / 音量，或旋钮、触摸 |
| 音频输出 | 耳机 / 有源音箱 | 小功放 + 扬声器，按小喇叭调音量与 BASS |
| 供电 | 开发板供电 | 电池 + 分区供电 + 待机唤醒 |
| 形态 | 杜邦线验证板 | CD 盒比例外壳、背面磁贴、可换 TF 卡 |

### 从验证板到产品：待办清单

> 以下均**尚未实现**，仓库中没有 EPD 驱动、封面解析、ID3 解析、电池管理或结构相关代码。

1. **屏幕选型与驱动**：CD 封面是方形，优先考虑方形/近方形 EPD（如 2.9"、3.7"、4.2"），黑白或三色；SPI 接口 + Busy 引脚 + 全刷/局刷 LUT + 深度睡眠。建议新建 `HARDWARE/EPD` 驱动目录，与 `HARDWARE/LCD` 模板区分开。
2. **SPI 总线分配**：EPD 与 VS1003 都要 SPI。共用 SPI1 时必须有独立片选，且**墨水屏一次全刷可能持续数百毫秒，会打断喂数据**；更稳妥的做法是让 EPD 走工程里已保留的 **SPI2**（`SPI2_Init()` 已存在），或在 DREQ 空闲窗口内分片刷新，靠双缓冲余量顶住。
3. **封面素材**：两种来源——解析 MP3 的 ID3v2 `APIC` 内嵌封面，或每曲配同名图片。F103 内存有限，建议在 PC 端预转成 **EPD 原生 1bpp 位图**（例：200×200 约 5KB/张）存到 TF 卡 `COVER/` 目录，播放时直接按短名索引送屏，避免运行时解码 JPEG / 缩放。
4. **元数据与显示内容**：解析 ID3v1/ID3v2 拿曲名、专辑、艺术家；`VS_Get_HeadInfo()`（码率）与 `VS_Get_DecodeTime()`（秒数）已有，可直接用于显示。要显示中文曲名需开启 FatFs 的 `_USE_LFN`（当前为 0）并配 cc936。
5. **刷新策略**：切歌时全刷一次封面；播放中只做低频局部刷新（进度/时间），墨水屏刷新慢且有闪烁，不做实时动画。暂停 / 待机时保持最后一帧封面。
6. **低功耗与电源**：VS1003、TF 卡、EPD 分区供电；播放结束进待机、按键唤醒；利用 EPD 断电保图，让「没在播的时候也挂着一张封面」。
7. **音频与结构**：耳机口改小功放 + 扬声器（代码注释里已有小喇叭用 `0x0044` 低音配置的建议）；CD 盒比例外壳、透明窗口/边框、背面磁铁或磁贴、电池仓、可插拔 TF 卡。
8. **曲库管理**：现有扫描只认根目录 8.3 短名、最多 32 首；产品化需要按专辑分目录、播放列表、封面索引，以及插卡后重扫。

---

## 三、硬件连接

| 模块 | 接口 | STM32 引脚 | 说明 |
| --- | --- | --- | --- |
| TF 卡（microSD） | SDIO 4-bit | PC8~PC12（D0~D3 / CLK）、PD2（CMD）、PD7（CD） | 初始化 400kHz，识别后切 4 位宽总线；当前为 **轮询模式**（DMA 模式代码保留但被注释） |
| VS1003 数据口 SDI | SPI1 | PA5（SCK）、PA6（MISO）、PA7（MOSI） | 主机模式、8 位帧、MSB 优先、CPOL=0/CPHA=1；`EPD_SPI_BaudRate = 8 分频`，即 PCLK2 72MHz / 8 ≈ **9MHz** |
| VS1003 控制口 SCI | GPIO | PA8（XCS）、PA4（XDCS） | 写寄存器 / 写数据分别片选 |
| VS1003 握手与复位 | GPIO | PB0（DREQ，上拉输入）、PE11（RST） | DREQ 高 = 可接收数据；低 = FIFO 满、解码中 |
| 按键 | GPIO | PA0（WK_UP）、PE4（KEY0） | WK_UP：下一曲；KEY0：暂停 / 继续 |
| 指示灯 | GPIO | PB5（LED0）、PE5（LED1） | LED0 用于初始化失败、无音频文件等状态提示 |
| 其他 | SPI2 / FSMC | W25QXX、LCD | 工程模板保留的板载资源，主流程中未实际用于播放；**SPI2 可作为后续 EPD 屏的独立总线**，LCD 是 TFT 模板而非墨水屏 |

> 说明：原始描述中的「TFT 卡」应为 **TF 卡（microSD）**；SPI 连的是 VS1003 解码芯片，LCD / W25QXX 属模板保留代码，不参与播放链路。

---

## 四、软件结构

基于正点原子（ALIENTEK）战舰 STM32F103 标准外设库例程裁剪，关键自研逻辑集中在 `HARDWARE/VS10XX/vs10xx.c` 与 `USER/main.c`。

| 层次 | 文件 | 职责 |
| --- | --- | --- |
| 应用入口 | `USER/main.c` | 初始化外设 → 挂载 SD 卡 → 扫描根目录音频 → 无限循环播放列表 |
| 播放控制 | `HARDWARE/VS10XX/vs10xx.c` | VS10XX 复位 / 寄存器读写 / 音效设置；**双缓冲播放 `VS_PlayFile()`**、**每曲复位与收尾 `VS_EndOfSong()`**；**目录扫描 `VS_ScanRootAudio()`** |
| 解码芯片底层 | `HARDWARE/VS10XX/vs10xx.h` | SCI 寄存器、SPI_MODE 位定义、引脚宏、播放列表容量 |
| 备选驱动（未编译） | `HARDWARE/VS10XX/vs1003.c/.h` | SPI1_TX **DMA1_Ch3 + EXTI0(DREQ 上升沿) + 2×2048B 缓冲**的「真并行」方案 |
| 存储 | `HARDWARE/SDIO/sdio_sdcard.c`、`FATFS/src/diskio.c` | SDIO 卡识别 / 多块读写；FatFs 磁盘 IO 适配 |
| 文件系统 | `FATFS/src/ff.c`（R0.11）、`FATFS/exfuns/exfuns.c` | FAT/FAT32；`exfuns_init()` 用内存池为 `FATFS`、`FIL`、`fatbuf` 分配空间 |
| 基础支撑 | `SYSTEM/`、`MALLOC/`、`HARDWARE/SPI/`、`LED/`、`KEY/` | 延时、内存池、SPI、按键与 LED |
| 规划中 | `HARDWARE/EPD/`（不存在） | 电子墨水屏驱动、封面位图送显、刷新策略 |

**FatFs 关键配置**（`FATFS/src/ffconf.h`）：`_USE_LFN = 0`（仅 8.3 短文件名）、`_CODE_PAGE = 936`（GBK / cc936）、`_VOLUMES = 2`、`_MIN_SS = _MAX_SS = 512`、`_FS_TINY = 0`。这决定了目录扫描只能识别大写短名后缀 `.MP3` / `.WAV`。

---

## 五、运行流程

1. **上电初始化**：延时、NVIC 分组 2、LED / 按键 / W25QXX 初始化、`VS_Init()`（配置 PB0/PE11/PA8/PA4 并初始化 SPI1）、内部 SRAM 内存池 `my_mem_init()`。
2. **卡与文件系统**：`SD_Init()` 上电、识别卡型、切 4 位总线并设置传输时钟，失败时 LED0 以 500ms 闪烁等待；`exfuns_init()` → `f_mount(fs[0], "0:", 1)` → `exf_getfree("0:", ...)` 校验 FAT 卷，失败同样 LED 闪烁等待。
3. **扫描曲库**：`VS_ScanRootAudio()` 打开 `0:/`，逐项 `f_readdir()`，跳过子目录，用 `strstr()` 匹配 `.MP3` / `.WAV`，把短文件名存入 `audio_file_list[MAX_AUDIO_FILE_NUM][FN_BUF_LEN]`（最多 **32** 首，`FN_BUF_LEN = 13`）。扫描结果按目录物理顺序，未排序。若无音频文件，LED0 以 300ms 闪烁并停在此处。
4. **循环播放**：`for(idx = 0 .. audio_file_cnt-1)` 逐首调用 `VS_PlayFile(full_path)`（路径显式拼为 `0:/xxx.MP3`）。**每首开头**都在 `VS_PlayFile()` 内执行 `VS_HD_Reset()` → `VS_Soft_Reset()` → `set10XX()`：重新识别芯片 ID、写 `SPI_CLOCKF = 0x9800`（3.5× 倍频 + 2×ADD）、恢复 SPI 到 9MHz、重设音效与音量，并清掉上一曲的解码状态与 SDI FIFO。
5. **音效参数**：`VS10XXram[5] = {0,0,3,2,230}` 拼出 `SPI_BASS = 0x0032`（人声向、低频小幅提升），`SPI_VOL = 0x2020` 为初始音量。

> 产品形态下，第 3 步还应同时读取封面与元数据；第 4 步每首开头的复位间隙（约 100~150ms）正好用来触发一次 EPD 封面全刷，不额外占用播放时间。

---

## 六、双缓冲喂数据：为什么不会卡

### 关键参数

| 项 | 值 | 位置 |
| --- | --- | --- |
| 缓冲区 | `buf0[512]` / `buf1[512]`，静态分配，合计 1KB | `vs10xx.c:33-35` |
| 单次 SPI 推送块 | 最多 `VS_CHUNK_SIZE = 32` 字节 | `vs10xx.c:32` |
| 收尾补零 | VS1003 **2048** / VS1053 **2052** 字节 0x00，按 32 字节分块 | `VS_FlushFifo()` |
| 每曲复位 | 硬复位 → 软复位 → `set10XX()` | `VS_PlayFile()` 开头 |
| 流控信号 | `VS_DQ`（PB0 / DREQ） | `vs10xx.h:72` |

### 工作机制

`VS_PlayFile()` 用 `send_buf` / `fill_buf` 两个指针指向两块缓冲区，进入循环前先预读第一块到 `send_buf`：

```
每曲开头：VS_HD_Reset() → VS_Soft_Reset() → set10XX()
预读第 1 块 → send_buf
while (1):
    扫描按键（切歌 / 暂停）
    if DREQ == 0:                       # 芯片 FIFO 满，正在解码
        if send_buf 已发空 且 未到文件尾:
            f_read(fill_buf, 512)       # 阻塞读卡，但此刻 CPU 本来就在等解码
            交换 send_buf / fill_buf    # 预读块变成待发块
        delay_us(20); continue
    if DREQ == 1:                       # 可以喂数据
        if send_buf 还有数据:
            以 ≤32 字节为单位经 SPI1 写入 SDI（XDCS 拉低→拉高）
        else:
            f_read(fill_buf, 512) 并交换  # 读卡慢于解码时的兜底补读
    if 到文件尾 且 send_buf 发空: break
f_close() → VS_EndOfSong()：SM_CANCEL（仅 VS1053）+ 补 2048/2052 字节 0x00 + 清 SPI_DECODE_TIME
```

**要点**

- **读卡与解码在时间上错开**：SD 卡的阻塞读取被安排在 DREQ 拉低期间，此时 VS1003 正在消耗片内数据，SD 读的抖动不会立刻反映为断流；数据到达后只需交换两个指针，无内存拷贝开销。
- **32 字节分块贴合 DREQ 语义**：VS10XX 在 DREQ 为高期间一次建议接收 32 字节，每次发送前后都重新判断 DREQ，避免 FIFO 溢出。
- **两条补读路径**：DREQ 低时预读（正常路径）+ DREQ 高但缓冲已空时立即补读（异常兜底），保证高码率或慢卡情况下逻辑不死锁。
- **退出干净**：文件结束后 `f_close()` → `VS_EndOfSong()`，用 2048/2052 字节零把 SDI FIFO 彻底冲净（原来只发 32 字节，残留的解码样本就是切歌尾音的来源），再清零 `SPI_DECODE_TIME`；VS1053 会先置 `SM_CANCEL`（VS1003 的 MODE bit3 是 `SM_OUTOFWAV`，不能复用该位）。
- **每曲独立复位**：解码器复位从 `main.c` 的「仅每轮第 0 首」移入 `VS_PlayFile()` 开头，每首执行；软复位里无超时的 `while(VS_DQ==0)` 也改成带超时等待，避免每首复位时芯片异常导致死等。
- **切歌标志**：`g_next_song_flag` 在每曲开头复位，配合上述收尾，切歌不会把上一曲状态带进下一首。

> 准确定位：当前构建里这是 **软件乒乓缓冲 + 轮询 SPI**，读卡与发送是「时间上重叠、隐藏延迟」，并非 DMA 并行。真正「读 SD 与 SPI 发送同时跑」的实现是未纳入工程的 `vs1003.c`（DMA + DREQ 外部中断），详见第九节。接上墨水屏后，屏刷新同样应避开喂数据的关键窗口。

### 交互

- `WK_UP`（PA0）：置 `g_next_song_flag`，立即跳出当前曲目，进入下一首。
- `KEY0`（PE4）：翻转 `g_play_pause_flag`；暂停时停止喂数据、只轮询按键（10ms 一次），同时把 `SPI_VOL` 增大一档（音量降低）。
- 播放循环每轮都会重新扫描标志位，因此切歌响应在 512 字节量级内。

---

## 七、构建与验证步骤

1. 用 Keil MDK 打开 `USER/EPD.uvprojx`，目标器件 STM32F103ZE，宏定义 `STM32F10X_HD, USE_STDPERIPH_DRIVER`，优化等级 `Optim=1`；编译产物在 `OBJ/`（`EPD.hex` / `EPD.axf`，本次修复后已重新构建，0 Error / 0 Warning）。
2. 把自己的 MP3/WAV 文件拷到 TF 卡**根目录**（仓库不含音频素材，请自备并注意版权）；卡建议格式化为 FAT32；文件名用 8.3 短名且后缀大写（如 `01.MP3`），因为 `_USE_LFN = 0`。
3. 插卡、接好 VS1003 模块与耳机/音箱，下载固件或烧录 `OBJ/EPD.hex` 后复位。
4. 正常：LED0 熄灭 / 常亮后开始按顺序循环播放；无卡或文件系统异常：LED0 500ms 闪烁；根目录无音频：LED0 300ms 闪烁。
5. 调试接口：`VS_Sine_Test()`（正弦测试，验证 SPI/模拟输出）、`VS_Ram_Test()`（芯片 RAM 自检）、`VS_Get_HeadInfo()`（读码率）、`VS_Get_DecodeTime()`（解码秒数）。串口打印与 USMART 在 `main.c` 中被注释，需要时打开 `uart_init(115200)` 与 `Printf`。

**当前固件体积**（本次重新构建）：Code=37084、RO-data=12264、RW-data=156、ZI-data=1096420；ZI 约 1MB 主要来自内存池中映射到外部 SRAM 的区域。

---

## 八、已验证的能力与实现边界

### 已实现

- SDIO 4-bit 读 TF 卡 + FatFs 挂载、容量检查、根目录扫描；
- VS1003 硬/软复位、芯片 ID 自动识别（VS1003 / VS1053 分支）、时钟倍频、BASS/音量设置；
- 双缓冲 + DREQ 流控的连续播放；曲目循环、切歌、暂停；
- 每曲独立复位 + 完整 FIFO 冲刷（本次修复，消除切歌尾音）；
- LED 故障提示、正弦/RAM 测试、码率与解码时间读取。

### 边界与建议改进（均对应现有代码）

> 其中 **#1 / #3 / #4 已在本次修复中解决**，详见第十节。

| # | 位置 | 问题 | 建议 |
| --- | --- | --- | --- |
| 1 ✅ | `main.c` | ~~拼了 `full_path` 却未使用，依赖「当前目录 = 根目录」~~ **已修复** | 现为 `VS_PlayFile(full_path)`，显式使用 `0:/` 路径 |
| 2 | `vs10xx.c:433-443` | 「暂停」只是停止喂数据并把音量降一档，恢复播放时**没有还原音量**，每按一次音量就小一档；且未真正静音 | 暂停写 `SPI_VOL = 0xFEFE`（或 `SM_CANCEL` 清解码），恢复时写回保存的 `volt` |
| 3 ✅ | `vs10xx.c` | ~~曲末只发 32 字节 0x00，切歌尾音~~ **已修复** | `VS_EndOfSong()`：SM_CANCEL（VS1053）+ 2048/2052 字节零 + 清解码时间 |
| 4 ✅ | `main.c` | ~~每轮仅第一首复位，解码状态跨曲继承~~ **已修复** | 复位移入 `VS_PlayFile()` 开头，每曲执行 |
| 5 | `vs10xx.c:535-577`、`ffconf.h:96` | 只扫根目录、只认 `.MP3/.WAV` 短名、`strstr` 匹配不够严谨、最多 32 首且超出静默丢弃、按目录顺序不排序 | 开启 `_USE_LFN`（配合 cc936 支持中文长名）、后缀精确比较、递归子目录、排序、列表容量参数化 |
| 6 | `vs10xx.c:33-35` | 512B 缓冲在 128kbps 下约覆盖 31ms；320kbps 或慢卡时余量明显变小 | 加大到 1024/2048B，或启用第九节的 DMA 双缓冲方案 |
| 7 | `sdio_sdcard.c:111-112` | SDIO 固定为**轮询模式**，DMA 模式被注释 | 高码率场景可切 `SD_SetDeviceMode(SD_DMA_MODE)` 降低 CPU 占用 |
| 8 | `main.c:27-28` | 串口 / USMART 全部注释，出错时看不到 `f_open` 返回码、文件名与码率 | 打开串口日志，打印扫描结果、`FRESULT`、码率与解码时间 |
| 9 | 全局 | 播放列表仅上电扫描一次，无卡检测中断，运行中换卡需复位 | 增加卡检测 / 定时重扫 |
| 10 | 全局 | **电子墨水屏、封面资源、ID3 元数据、电池与外壳均未在代码中实现**；`HARDWARE/LCD` 只是 TFT 模板 | 按第二节的产品化清单逐项补齐 |

---

## 九、关于 `vs1003.c`：一套未启用的「真并行」方案

`HARDWARE/VS10XX/vs1003.c/.h` 提供了另一套更彻底的实现，思路是：

- 播放数据交给 **SPI1_TX → DMA1_Channel3** 自动搬运，`DREQ(PB0)` 接 **EXTI0 上升沿中断**，DMA 传输完成中断里切换缓冲并再次启动，全程不占主循环；
- 主循环只负责从 SD 卡 `f_read()` 填缓冲，通过 `VS1003_GetFillBuffer()` / `VS1003_CommitBuffer()` 与中断侧交接；
- 缓冲区 **2 × 2048B**，按注释在 128kbps 下可撑约 125ms（注释写 250ms 对应 4096B 配置，实际宏为 2048B）；
- 对外接口：`VS1003_Init()`、`VS1003_Play(filename)`。

**但当前 `USER/EPD.uvprojx` 的文件组里只有 `vs10xx.c`，没有 `vs1003.c`；`USER/EPD.map` 中也不存在任何 `vs1003` 符号**，`OBJ/vs1003.o` 属于早期构建残留。也就是说，实际运行的是 `vs10xx.c` 的轮询乒乓方案。若要切换到 DMA 方案，需要：把 `vs1003.c` 加入工程、协调 `VS_Init()` 与本驱动的 GPIO/SPI 初始化（含 DREQ 的 EXTI 配置）、把 `main.c` 的播放调用改为 `VS1003_Play(full_path)`，并确认 `EXTI0_IRQHandler` / `DMA1_Channel3_IRQHandler`（定义在 `vs1003.c` 内）与 `stm32f10x_it.c` 不冲突。

> 对产品形态而言，DMA 方案还有一层价值：把 CPU 从「盯 DREQ 逐字节发」里解放出来，才有余力在切歌时处理封面送显、ID3 解析与墨水屏刷新。

---

## 十、本次修复记录（切歌尾音 / 解码状态跨曲复用）

**现象**：切歌时上一曲的尾音会漏到下一曲开头。

**根因**

1. `VS_PlayFile()` 曲末只发 32 字节 `0x00`，远小于 VS1003 的 SDI FIFO 深度，残留的解码样本仍留在 FIFO / 解码器里；
2. `main.c` 只在每轮循环的第 0 首做硬复位 + 软复位，其余曲目直接续播，解码器状态被跨曲继承。

**改动**（`HARDWARE/VS10XX/vs10xx.c`、`USER/main.c`；保持原有 GB18030 编码与 CRLF 换行，新增中文注释与原文件编码一致）

- 新增 `VS_FlushFifo(u16 zero_bytes)`：按 32 字节分块、逐块等待 DREQ，发送 **2048（VS1003）/ 2052（VS1053）** 字节 `0x00`；带约 200ms 超时保护，芯片异常时不会死等。
- 新增 `VS_EndOfSong()`：VS1053 先置 `SM_CANCEL` 并等待清位（**VS1003 的 MODE bit3 是 `SM_OUTOFWAV`，不能复用该位**），再冲刷 FIFO，最后清 `SPI_DECODE_TIME`；替代原来的 32 字节零。
- `VS_PlayFile()` 开头加入 `VS_HD_Reset()` → `VS_Soft_Reset()` → `set10XX()`，**每首执行**；曲末改调 `VS_EndOfSong()`。
- `VS_Soft_Reset()` 中无超时的 `while(VS_DQ==0)` 改为带 2000×50µs 超时，避免每首复位时芯片异常导致死等。
- `main.c` 删除 `if(idx==0)` 条件复位块，并改为 `VS_PlayFile(full_path)`（原来拼好完整路径却没用）。
- 删除不再使用的 `end_bytes[32]` 与 `VS_SendData()`。

**验证**：Keil MDK 命令行 `UV4 -b USER/EPD.uvprojx` 重新构建，**0 Error(s), 0 Warning(s)**；`OBJ/EPD.hex` / `EPD.axf` 已重新生成。尾音是否彻底消除仍需上板试听确认（建议连续切 5~10 首、覆盖不同码率）。

**注意**

- 每首复位会增加约 100~150ms 的曲目间隔；产品形态下这段间隙可顺便用于墨水屏封面全刷。若觉得间隙偏长，可改为「软复位 + `SM_CANCEL`」组合，但不要退回只补 32 字节零的做法。
- 本次**未改动** KEY0「暂停只降音量、恢复时不还原」的旧行为，仍列在第八节改进表 #2。

---

## 十一、文档依据

本说明按仓库实际代码整理，主要依据：`USER/main.c`、`HARDWARE/VS10XX/vs10xx.c/.h`、`HARDWARE/VS10XX/vs1003.c/.h`、`HARDWARE/SPI/spi.c/.h`、`HARDWARE/SDIO/sdio_sdcard.c/.h`、`FATFS/src/ffconf.h`、`FATFS/src/diskio.c`、`HARDWARE/KEY/key.c`、`USER/EPD.uvprojx`、`OBJ/EPD.build_log.htm`（本地构建日志，不入库）。代码示例沿用原工程的 GBK 注释风格，本文档为 UTF-8 编码。

> 其中**第二节的命名缘由、产品定位与规划由项目方提供**，属于设计意图，不是当前代码事实；其余章节均为代码核对结果。

---

## 十二、开源与仓库说明

### 代码来源

本工程是在 **正点原子（ALIENTEK）战舰 STM32F103 标准外设库例程**基础上裁剪、修改而来，并包含以下第三方组件，各自版权与许可归原作者 / 原组织所有：

| 组件 | 目录 | 说明 |
| --- | --- | --- |
| STM32F10x 标准外设库、CMSIS、启动文件 | `STM32F10x_FWLib/`、`CORE/`、`SYSTEM/` | STMicroelectronics，按其原始许可分发 |
| FatFs R0.11 及 cc936/cc949/cc950 编码表、官方文档 | `FATFS/` | ChaN（Elm Chan），FatFs 许可 |
| VS10XX 驱动、SDIO / SPI / KEY / LED / MALLOC / USMART 等板级模板代码 | `HARDWARE/`、`MALLOC/`、`USMART/` | 正点原子例程模板，本项目在其上改写 |
| 播放链路与本次修复 | `HARDWARE/VS10XX/vs10xx.c`、`USER/main.c`、`README.md` | 本项目自研部分 |

**本仓库尚未指定开源许可证**：公开仅用于技术交流与学习参考；如需复用，请同时遵守上述各上游组件的许可条款并取得相应授权。若希望允许他人自由使用，请后续补充 `LICENSE` 文件。仓库**不包含**任何 MP3/WAV 音频素材、CD 封面图片或其他第三方媒体内容。

### 文件与编码约定

- `.gitignore` 排除 Keil 的 `.o/.d/.crf/.axf/.map/.lst/.htm` 等中间产物与含本机用户名的 `.uvguix.*`、`JLinkSettings.ini`；`OBJ/` 中仅保留可直接烧录的成品固件 `OBJ/EPD.hex`。clone 后用 Keil 打开 `USER/EPD.uvprojx` 重新 Build 即可。
- 源码原本是 **GB18030 编码 + CRLF**（Keil MDK 打开中文注释才不乱码）。仓库通过 `.gitattributes` 的 `working-tree-encoding` 把 `.c/.h/.s/.bat` 在**仓库内统一存为 UTF-8**，GitHub 网页可正常显示中文；`git clone` 时 Git 会自动转回 GB18030 + CRLF，本地编辑与编译不受影响。请用 Git 克隆而非下载 ZIP 获取源码，否则得到的是 UTF-8 版本，Keil 中注释会乱码。
- `HARDWARE/VS10XX/vs1003.c/.h`（未参与编译的 DMA 备选方案）本身已是 UTF-8，已在 `.gitattributes` 中单独排除转换。
- 源码中的引脚定义按**战舰 STM32F103 开发板**给出；换板时请核对第三节的连接表。

### 当前状态

播放链路已在 Keil 下编译通过（0 Error / 0 Warning），但**切歌尾音修复仍需上板试听确认**；电子墨水屏、CD 封面、ID3 元数据、电池与外壳均属于规划阶段，尚未实现。详见第八、十节。


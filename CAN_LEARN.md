# CAN_LEARN.md — CAN 总线理论知识笔记

> 本文档记录项目过程中对 CAN 总线原理、机制、设计的理解。
> 操作规则、踩坑记录、API 索引请参见 [CLAUDE.md](CLAUDE.md)。

---

## 1. CAN 滤波器

### 1.1 是什么？

CAN 总线是**广播式**的——总线上所有节点都能收到所有帧。如果每个节点都要用 CPU 处理每一帧，CPU 会被无关消息淹死。

CAN 硬件滤波器就是**在硬件层面帮你筛选**：只有你关心的 ID 的消息才进 FIFO → 触发中断 → 唤醒 CPU。

```
                    CAN 总线
                       │
                       ▼
               ┌───────────────┐
               │  CAN 控制器    │
               │               │
  帧 0x100 ──→│  ┌─────────┐  │
  帧 0x201 ──→│  │ 14 组   │  │──→ FIFO0 ──→ 中断 ──→ CPU 被唤醒
  帧 0x500 ──→│  │ 硬件滤  │  │
  帧 0x300 ──→│  │ 波器    │  │──→ ❌ 丢弃（不进 FIFO，CPU 完全不知道）
  帧 0x7FF ──→│  └─────────┘  │
               └───────────────┘
```

> **本质**：CAN 滤波器 ≠ 加密/密码，而是**硬件层的"身份证检查"**。每个 CAN 帧的 ID 就是它的身份证号，滤波器只看身份证号，不读数据内容。

### 1.2 生活类比

```
你去参加一个技术交流会（CAN 总线）

┌─────────────────────────────────────────────────────────┐
│                    CAN 总线（会议室）                     │
│                                                         │
│   Node A 喊： "0x100 心跳！"    "0x101 传感器数据！"     │
│   Node B 喊： "0x200 心跳！"    "0x201 传感器数据！"     │
│   Node C 喊： "0x500 紧急停机！"                         │
│                                                         │
│  ┌─ 你的耳朵（Node B 的 CAN 控制器）──────────────┐     │
│  │                                                  │     │
│  │  滤波器 = 你戴的"选择性耳机"                      │     │
│  │                                                  │     │
│  │  全通模式 (Mask=0)：所有声音都进耳朵              │     │
│  │  精确模式：只听"0x200"和"0x300"开头的喊话        │     │
│  │  关闭模式 (Bug)：耳机被摘了，什么都听不到！       │     │
│  │                                                  │     │
│  └──────────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────┘
```

### 1.3 STM32F1 滤波器配置

STM32F1 有 **14 个滤波器组**（Filter Bank 0~13），每个组独立配置。

#### 两种匹配模式

| 模式 | 说明 |
|------|------|
| **ID 掩码模式** (`CAN_FILTERMODE_IDMASK`) | 掩码位=1 必须匹配，掩码位=0 不关心 → 可以匹配一个范围的 ID |
| **ID 列表模式** (`CAN_FILTERMODE_IDLIST`) | 精确匹配指定 ID → 32 位模式每组 2 个 ID，16 位模式每组 4 个 |

#### 两种位宽

| 位宽 | 每组 ID 数 | 能匹配扩展帧？ |
|------|-----------|---------------|
| 32 位 (`CAN_FILTERSCALE_32BIT`) | 2 个 | ✅ |
| 16 位 (`CAN_FILTERSCALE_16BIT`) | 4 个 | ❌ 只能标准帧 |

#### 32 位寄存器中 ID 的存放位置

```
32 位 CAN 滤波器寄存器布局（标准帧 11-bit ID）：

  位:  31 30 29 28 27 26 25 24 23 22 21  |  20 ... 0
       ├────────── STID[10:0] ──────────┤  ├─ EXID[17:0] ─┤
       │    标准帧 ID (左对齐到高位!)    │   扩展帧专用    │

  例子：ID = 0x200 (二进制: 10 0000 0000)
  寄存器中存为: 0x200 << 5 = 0x4000
       ↑ 左移 5 位是因为 STID 在 bit[31:21]
```

#### 配置全通滤波器（调试用）

```c
CAN_FilterTypeDef sFilterConfig;
sFilterConfig.FilterBank = 0;
sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
sFilterConfig.FilterIdHigh = 0x0000;        // 期望 ID = 全 0
sFilterConfig.FilterIdLow  = 0x0000;
sFilterConfig.FilterMaskIdHigh = 0x0000;    // 掩码 = 全 0 → 不检查任何位 → 全通！
sFilterConfig.FilterMaskIdLow  = 0x0000;
sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
sFilterConfig.FilterActivation = ENABLE;
sFilterConfig.SlaveStartFilterBank = 14;
HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
```

> **为什么 Mask=0 就是全通？** Mask 某位=0 表示该位不检查。全部 0 = 全部位不检查 → 任何 ID 都通过。

#### 配置精确匹配（正式用）

```c
// 只接收 ID=0x200 和 ID=0x201 的帧
#define CAN_FILTER_ID(id)  ((uint32_t)(id) << 5)

sFilterConfig.FilterBank = 0;
sFilterConfig.FilterMode = CAN_FILTERMODE_IDLIST;    // 列表模式
sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;   // 32 位 → 一组放 2 个 ID
sFilterConfig.FilterIdHigh = CAN_FILTER_ID(0x200);   // 第一个 ID
sFilterConfig.FilterIdLow  = CAN_FILTER_ID(0x201);   // 第二个 ID
sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
sFilterConfig.FilterActivation = ENABLE;
HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
```

---

## 2. CAN 回环自测（Loopback）

### 2.1 是什么？

STM32 CAN 外设内置的**自测模式**——CAN 控制器内部把 TX 和 RX 接通，发送的帧直接回到接收端。

### 2.2 两种回环模式

| | `CAN_MODE_LOOPBACK` | `CAN_MODE_SILENT_LOOPBACK` ✅ |
|---|---|---|
| 内部回环 (TX→RX) | ✅ | ✅ |
| CAN_TX 引脚 | ⚠️ 驱动引脚，信号会到总线上 | ✅ **高阻态**，不干扰总线 |
| CAN_RX 引脚 | ⚠️ 同时接收总线信号（可能冲突） | ✅ 断开，只听内部回环 |
| 对总线影响 | ❌ 可能干扰其他节点 | ✅ 完全不影响 |
| 适用场景 | 单芯片独立测试 | **单芯片不干扰总线 / 多节点中静默监听** |

### 2.3 为什么回环自测不需要 SN65HVD230？

```
CAN_MODE_SILENT_LOOPBACK 模式下的数据流：

STM32 内部                    STM32 引脚               SN65HVD230
┌──────────────┐         ┌─────────────────┐         ┌──────────┐
│              │         │                 │         │          │
│ TX ──→ RX    │   ╳    │ PA12 → 高阻态   │   ╳    │ 完全     │
│ (内部回环)   │  断开   │ (不驱动信号)    │  断开   │ 不受     │
│              │         │                 │         │ 影响     │
│              │   ╳    │ PA11 → 断开     │   ╳    │          │
│              │  断开   │ (忽略外部)     │  断开   │          │
└──────────────┘         └─────────────────┘         └──────────┘
```

- **不需要 SN65HVD230**：回环在 STM32 内部完成
- **不需要拔线**：静默模式 TX 引脚高阻态，不干扰外部
- **不需要两个 STM32**：一个芯片自己发自己收

### 2.4 LED 如何证明 CAN 工作正常？

```
上电启动
  │
  ├─① CAN_App_Init() 成功 → CAN 启动
  │   失败 → Error_Handler()，LED 不会闪
  │
  ├─② FreeRTOS 启动，三个任务开始运行
  │
  ├─③ CanTxTask 发送心跳帧 → 内部回环 → CanRxTask 收到
  │   ↓
  │   peer_online 从 0 → 1
  │
  ├─④ LED 从慢闪 (500ms) → 心跳闪烁 (亮100ms/灭1900ms)
  │
  └─⑤ 串口输出启动信息

LED 诊断表：
┌────────────────────┬──────────────────┬──────────────────────┐
│ LED 状态           │ 含义              │ 反推问题             │
├────────────────────┼──────────────────┼──────────────────────┤
│ 不亮/常亮          │ 程序卡死          │ CAN 初始化失败       │
│ 慢闪 (500ms)       │ 任务跑但收不到帧   │ 滤波器/中断/回环未开 │
│ 快闪 (200ms)       │ 收到帧但有丢帧     │ 序列号连续性检查报警 │
│ 心跳闪烁 (100ms)   │ ✅ CAN 全链路正常  │ —                    │
└────────────────────┴──────────────────┴──────────────────────┘
```

---

## 3. CubeMX CAN 初始化到底做了什么？

CubeMX 生成的 `MX_CAN_Init()` **只配置底层硬件参数**，不启动 CAN：

```c
// CubeMX 做的（在 MX_CAN_Init 中）：
✅ 使能 CAN1 时钟
✅ 配置 GPIO (PA11=RX, PA12=TX)
✅ 波特率分频 (Prescaler=9 → 500Kbps)
✅ 位时序 (BS1=3TQ, BS2=3TQ, SJW=1TQ)
✅ 工作模式 (NORMAL / LOOPBACK 等)
✅ AutoBusOff / AutoRetransmission 等选项
✅ HAL_CAN_Init() → 写入寄存器

// CubeMX 不做的（需要自己写代码）：
❌ 配置滤波器        → HAL_CAN_ConfigFilter()
❌ 启动 CAN (退出 INIT) → HAL_CAN_Start()
❌ 使能中断通知       → HAL_CAN_ActivateNotification()
❌ NVIC 中断使能     → HAL_NVIC_EnableIRQ()
```

> CubeMX 只搭好了 CAN 外设的"骨架"，**滤波器、中断、启动**这三步必须自己写代码。

---

## 4. CAN 总线多设备通信设计

### 4.1 最多可以接多少个设备？

| 限制因素 | 瓶颈值 |
|---------|--------|
| CAN 协议 | 理论上无上限（标准帧有 2048 个 ID） |
| SN65HVD230 驱动能力 | ≈ 120 个节点 |
| 总线电容（信号质量） | 节点越多 → 边沿越慢 → 需降波特率 |
| **实际经验值：** |
| 500 Kbps | ≤ 64 个节点 |
| 250 Kbps | ≤ 100 个节点 |
| 125 Kbps | ≤ 120 个节点 |

### 4.2 ID 分段编码方案（行业标准）

```
CAN 标准帧 11 位 ID 分段设计：

  Bit:  10  9  8  7  6  5  4  3  2  1  0
       ├────┤├────────┤├────────────────┤
       优先级   节点地址    消息类型
       (0~7)   (0~31)    (0~31)

  例子：
  0x200 = 10 0000 0000
          │  │        │
          优先=4  节点=0   消息=0 (心跳)
          
  0x201 = 10 0000 0001
          │  │        │
          优先=4  节点=0   消息=1 (传感器)
          
  0x300 = 11 0000 0000
          │  │        │
          优先=6  节点=0   消息=0 (广播命令)
```

| 段 | 位宽 | 含义 | 示例 |
|----|------|------|------|
| 优先级 | 3 bits (0~7) | 越小优先级越高，决定总线仲裁 | 紧急停机=0，心跳=4 |
| 节点地址 | 5 bits (0~31) | 目标节点，0=广播 | Node B=1, Node C=2 |
| 消息类型 | 3 bits (0~7) | 帧用途 | 心跳=0, 传感器=1, 命令=2, ACK=3 |

### 4.3 多设备滤波器策略

**调试阶段**：全通滤波器（Mask=0），方便排查问题。

**正式阶段**：每个节点只接收发给自己的帧 + 广播帧。

```c
// Node B (地址=0x01) 的滤波器：
// 接收：节点地址匹配 0x01（任何优先级、任何消息类型）+ 广播 (节点地址=0x00)

// 滤波器组 0：接收发给 Node B 的帧 (节点地址=0x01)
sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
// 只检查节点地址字段，不检查优先级和消息类型位
sFilterConfig.FilterIdHigh   = (0x01 << 5) << 8;   // 节点地址=0x01
sFilterConfig.FilterMaskIdHigh = (0x1F << 5) << 8; // 掩码：只检查节点地址位
sFilterConfig.FilterMaskIdLow  = 0x0000;            // 低 16 位不检查

// 滤波器组 1：接收广播帧 (节点地址=0x00)
sFilterConfig.FilterIdHigh = (0x00 << 5) << 8;  // 广播地址
// ... 其余相同
```

---

## 5. CAN 协议帧设计原则

### 5.1 核心原则

1. **8 字节有限，一帧只做一件事** —— 不要在帧里混搭多种类型数据
2. **帧 ID = 身份证，帧类型 = 信封抬头** —— 先看 ID，再看 Data[0]
3. **序列号四作用**：检测丢帧、检测重复、判断新鲜度、计算通信质量

### 5.2 帧格式模板

```
Frame[0]       Frame[1]       Frame[2..7]
帧类型(TYPE)   子类型/序列号  载荷数据
 │              │              │
 ├ 0x01 心跳    ├ state        ├ [保留...]
 ├ 0x02 传感器  ├ counter      ├ [sensor1_H][sensor1_L][sensor2_H][sensor2_L]...
 ├ 0x03 命令    ├ cmd_code     ├ [cmd_param...]
 ├ 0x04 ACK     ├ ack_cmd      ├ [result][...]
 └ 0x05 事件    ├ event_code   ├ [event_data...]
```

### 5.3 CAN 硬件 ACK ≠ 应用层 ACK

```
CAN 硬件 ACK：
  "数据帧 CRC 正确，至少一个节点在物理层收到了"
  = 快递已签收（可能是前台签的）

应用层软件 ACK：
  "数据完整，校验通过，业务逻辑处理完成"
  = 邮件已读回执（本人确认）

解决方案：应用层命令 → ACK 帧应答 → 超时重发（最多 3 次）
```

### 5.4 序列号（Sequence Number）的四种作用

| 作用 | 说明 | 示例 |
|------|------|------|
| 检测丢帧 | 收到 seq=3→5（缺了 4） | 知道中间丢了一帧 |
| 检测重复 | 连续收到两个 seq=7 | 知道有一帧被重传了 |
| 判断新鲜度 | seq=2 vs seq=200 | 旧的忽略，新的处理 |
| 计算通信质量 | 丢帧数 / 总发送数 | 丢帧率统计 |

---

## 6. 常见误区

| 误区 | 正确理解 |
|------|---------|
| "CubeMX 配好 CAN 就行了" | CubeMX 只配底层，滤波器/中断/启动需手动写 |
| "CAN 硬件 ACK = 对方收到了" | 硬件 ACK 只保证物理层送达，不保证应用层处理 |
| "RTR 遥控帧 = 软件层请求" | 现代 CAN 用普通数据帧实现请求-应答，RTR 几乎没人用 |
| "回环测试需要收发器" | 静默回环全在芯片内部，不需要 SN65HVD230 |
| "滤波器=密码/加密" | 滤波器只检查帧 ID，不看数据内容，不做加密 |

---

*最后更新：2026-06-19*

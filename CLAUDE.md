# CLAUDE.md — CAN 多节点通信项目（STM32F103C8T6 + FreeRTOS）

> **项目名称**：can_node_a（CAN 总线多节点通信 Demo）
> **芯片**：STM32F103C8T6（Cortex-M3，64KB Flash，20KB SRAM）
> **IDE/工具**：STM32CubeMX 生成骨架 + VSCode + CMake + ARM GCC
> **CAN 收发器**：SN65HVD230（3.3V，最高 1Mbps）

---

## 关键规则

### ⚡ 规则 0：CLAUDE.md 持续优化
- **本项目 CLAUDE.md 是"活文档"**，随项目推进而不断演进
- 当遇到以下情况时，必须更新 CLAUDE.md：
  - 新增了协议层或应用层模块 → 更新目录结构和 API 索引
  - 解决了某个坑或踩雷 → 将经验写入对应规则或新增"踩坑记录"章节
  - 调整了任务架构或中断优先级 → 更新对应规则的决策依据
  - 发现了更好的代码组织方式 → 更新注释示例和编码规范
- **每次修改 CLAUDE.md 后，在对话中告知用户修改了什么**

### 1. CMake 修改规则
- **只允许修改根目录的 `CMakeLists.txt`**（即项目根路径下的那个）
- **严禁修改** `cmake/stm32cubemx/CMakeLists.txt`，该文件由 STM32CubeMX 自动生成，手动修改会在下次 CubeMX 重新生成时丢失
- 所有用户自定义的源文件、头文件路径、编译宏、链接库等，都添加到根 `CMakeLists.txt` 中对应的 `# Add user ...` 注释区域
- 添加 CAN 协议模块示例：
  ```cmake
  # Add user sources
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE
      Core/Src/can_protocol.c
      Core/Src/can_app.c
  )

  # Add user include directories
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
      # Add user defined include paths
  )
  ```

### 2. 源代码修改规则（USER CODE 区域）
- **所有用户代码必须写在 `/* USER CODE BEGIN X */` 和 `/* USER CODE END X */` 注释对之间**
- **严禁在 USER CODE 注释对之外写入任何代码**，否则会在 CubeMX 重新生成代码时被覆盖丢失
- 常见的 USER CODE 区域包括：
  - `USER CODE BEGIN Includes` / `USER CODE END Includes` — 用户头文件包含
  - `USER CODE BEGIN PV` / `USER CODE END PV` — 用户私有变量
  - `USER CODE BEGIN PFP` / `USER CODE END PFP` — 用户私有函数原型
  - `USER CODE BEGIN 0` / `USER CODE END 0` — 用户私有代码（main 函数外）
  - `USER CODE BEGIN 1` / `USER CODE END 1` — main 函数内最开头
  - `USER CODE BEGIN 2` / `USER CODE END 2` — 外设初始化之后
  - `USER CODE BEGIN 3` / `USER CODE END 3` — FreeRTOS 任务中 / while(1) 循环体内
  - `USER CODE BEGIN 4` / `USER CODE END 4` — 用户函数定义区域
  - `USER CODE BEGIN WHILE` / `USER CODE END WHILE` — while(1) 循环开头
  - `USER CODE BEGIN Application` / `USER CODE END Application` — freertos.c 末尾，适合放 CAN 中断回调等应用代码

### 3. CubeMX 生成文件清单（F103 版本）
以下文件由 STM32CubeMX 生成，修改时务必遵守规则 2（只写在 USER CODE 区域内）：
- `Core/Src/main.c`
- `Core/Src/stm32f1xx_it.c` ← 注意：F1 系列，不是 F4
- `Core/Src/stm32f1xx_hal_msp.c`
- `Core/Src/stm32f1xx_hal_timebase_tim.c`（FreeRTOS 下 HAL 时基由 TIM4 提供）
- `Core/Src/freertos.c` ← FreeRTOS 任务创建和初始化
- `Core/Src/can.c` ← CAN 外设初始化
- `Core/Src/usart.c` ← 串口初始化
- `Core/Src/gpio.c` ← GPIO 初始化
- `Core/Inc/main.h`
- `Core/Inc/stm32f1xx_it.h`
- `Core/Inc/stm32f1xx_hal_conf.h`
- `Core/Inc/FreeRTOSConfig.h`
- `Core/Inc/can.h`
- `Core/Inc/usart.h`
- `Core/Inc/gpio.h`

### 4. 项目目录结构
```
can_node_a/
├── CLAUDE.md                   ← 本文件（项目规则和索引）
├── CMakeLists.txt              ← 唯一允许修改的 CMake 文件
├── can_node_a.ioc              ← CubeMX 工程文件
├── cmake/
│   └── stm32cubemx/
│       └── CMakeLists.txt      ← CubeMX 生成，禁止修改
├── Core/
│   ├── Inc/                    ← 头文件
│   │   ├── main.h              ← CubeMX 生成
│   │   ├── FreeRTOSConfig.h    ← CubeMX 生成
│   │   ├── can.h               ← CubeMX 生成（CAN 外设句柄声明）
│   │   ├── can_protocol.h      ← 用户新建：CAN 协议定义
│   │   ├── can_app.h           ← 用户新建：CAN 应用层接口
│   │   ├── usart.h             ← CubeMX 生成
│   │   ├── gpio.h              ← CubeMX 生成
│   │   ├── stm32f1xx_it.h      ← CubeMX 生成
│   │   └── stm32f1xx_hal_conf.h← CubeMX 生成
│   └── Src/                    ← 源文件
│       ├── main.c              ← CubeMX 生成
│       ├── freertos.c          ← CubeMX 生成（任务函数体在此填充）
│       ├── can.c               ← CubeMX 生成（CAN 初始化）
│       ├── can_protocol.c      ← 用户新建：CAN 协议构造/解析
│       ├── can_app.c           ← 用户新建：CAN 应用层（中断回调+业务）
│       ├── usart.c             ← CubeMX 生成
│       ├── gpio.c              ← CubeMX 生成
│       ├── stm32f1xx_it.c      ← CubeMX 生成（中断向量表）
│       ├── stm32f1xx_hal_msp.c ← CubeMX 生成
│       ├── stm32f1xx_hal_timebase_tim.c ← CubeMX 生成
│       ├── system_stm32f1xx.c  ← CMSIS
│       ├── sysmem.c            ← 内存管理
│       └── syscalls.c          ← 系统调用
├── Drivers/                    ← HAL 库 + CMSIS（通常不修改）
│   ├── CMSIS/
│   └── STM32F1xx_HAL_Driver/
├── Middleware/
│   └── Third_Party/
│       └── FreeRTOS/           ← FreeRTOS 内核 + CMSIS_V2 封装
└── startup_stm32f103xb.s       ← 启动文件（Cortex-M3）
```

### 5. FreeRTOS 规则

#### 5.1 任务配置（CubeMX 中配置）
| 任务名 | 优先级 | 栈大小 | 说明 |
|--------|--------|--------|------|
| CanRxTask | osPriorityHigh | 256 words (1024B) | CAN 接收处理：等待队列消息 → 协议解析 → 业务处理 |
| CanTxTask | osPriorityNormal | 256 words (1024B) | CAN 周期发送：心跳帧 + 传感器数据帧 |
| LedTask | osPriorityNormal | 128 words (512B) | 状态指示：根据通信状态改变 LED 闪烁模式 |

> **设计决策：为什么 CanRxTask 优先级 > CanTxTask？**
> 
> CAN 消息是**异步到达**的，不受本节点控制。STM32F1 的 CAN 硬件 RX FIFO 只有 **3 级深度**——如果 CanRxTask 优先级低，CPU 正在跑 CanTxTask 的发送逻辑时，第 4 帧到来会导致硬件 FIFO 溢出丢帧。发送任务是**周期性主动行为**，它知道什么时候该发，可以等。接收任务永远不该等。
> 
> **铁律**：异步事件驱动的任务优先级 > 周期性主动任务。

#### 5.2 FreeRTOS 对象（CubeMX 中配置）
| 对象 | 类型 | 容量 | 说明 |
|------|------|------|------|
| CanMsgQueue | 消息队列 | 16条 × 48字节 | CAN 中断回调 → CanRxTask 的消息通道 |

> **设计决策：16 条 × 48 字节是否合理？**
> 
> - `can_msg_t` 实际大小约 20 字节（`#pragma pack(1)` 对齐），48 字节留有 2× 余量——万一后续消息结构体加字段（CRC、序列号、时间戳扩展等），不需要回来改队列配置
> - 16 条容量对于当前场景足够：CanRxTask 是 High 优先级，能在 130μs 内被调度并清空队列，所以队列不会积压到 16 条。但真正的瓶颈不是软件队列，而是 STM32F1 CAN 的**硬件 RX FIFO 只有 3 级深度**——如果中断响应被更高优先级 ISR 阻塞超过约 400μs，硬件 FIFO 就会溢出，软件队列再大也没用
> - 踩坑记录：[[#坑1-stm32f1-can-硬件-fifo-只有-3-级]]

#### 5.3 任务间通信架构
```
CAN RX 中断 (CAN1_RX0_IRQHandler)
    │
    ├─ HAL_CAN_IRQHandler(&hcan)
    │       └─ HAL_CAN_RxFifo0MsgPendingCallback()
    │               └─ osMessageQueuePut(CanMsgQueue, &msg, 0, 0)  ← 非阻塞，ISR 安全
    │
    ▼
CanMsgQueue (16条 × 48字节)
    │
    └─ CanRxTask: osMessageQueueGet(CanMsgQueue, &msg, NULL, osWaitForever)  ← 阻塞等待
           │
           ├─ 协议解析（帧类型判断 → 对应处理分支）
           ├─ 更新 sys_comm_status
           └─ 必要时通过全局标志触发 CanTxTask 应答

CanTxTask: osDelay(200ms) → 发送心跳帧
            osDelay(500ms) → 发送传感器数据帧

LedTask:   osDelay(100ms) → 根据 sys_comm_status 更新 LED 闪烁模式
```

#### 5.4 FreeRTOS 关键配置
- **内核版本**：FreeRTOS V10.3.1，CMSIS-RTOS V2 封装
- **调度方式**：抢占式（`configUSE_PREEMPTION = 1`）
- **优先级位数**：`configMAX_PRIORITIES = 56`（STM32F1 使用 4 位优先级，实际 16 级）
- **Tick 频率**：1000Hz（`configTICK_RATE_HZ = 1000`）
- **堆大小**：`configTOTAL_HEAP_SIZE = 8192`（heap_4.c）
- **时基**：TIM4 提供 HAL 时基（非 SysTick），避免与 FreeRTOS 调度器优先级冲突

#### 5.5 中断优先级规则（Cortex-M3，4 位优先级）
- `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`
  - 优先级 0~4：可在 ISR 中调用 FreeRTOS API（FromISR 版本）
  - 优先级 5~15：不可调用任何 FreeRTOS API
- **CAN RX 中断优先级建议**：设为 5（不可调用 FreeRTOS API 但通过 osMessageQueuePut 是 ISR 安全的）
  > ⚠️ 实际上 CMSIS V2 的 `osMessageQueuePut` 在 timeout=0 时会调用 `xQueueSendFromISR()`，可以在 ISR 中使用
- **USART1 中断优先级**：建议 ≥ 5，防止在中断中误调用阻塞 API 导致死锁
- **SVC/PendSV/SysTick**：FreeRTOS 内核使用，优先级由 port.c 自动设置，禁止手动修改

### 6. CAN 通信规则

#### 6.1 硬件连接
```
STM32F103 (Node A)                    STM32F103 (Node B)
    PA11 (CAN_RX) ←──→ SN65HVD230 ──→ CAN_H ──→ SN65HVD230 ←──→ PA11 (CAN_RX)
    PA12 (CAN_TX) ←──→              CAN_L              ←──→ PA12 (CAN_TX)
                                      │
                                   120Ω 终端电阻（两端各一个）
```

#### 6.2 CAN 配置参数（CubeMX 已配）
| 参数 | 值 | 说明 |
|------|-----|------|
| 波特率 | 500 Kbps | 36MHz APB1 / Prescaler(9) / (1+BS1(3)+BS2(3)) / = 500K |
| 模式 | CAN_MODE_NORMAL | 正常通信模式（调试时可改为 CAN_MODE_SILENT_LOOPBACK 自测） |
| SJW | CAN_SJW_1TQ | 同步跳转宽度 |
| BS1 | CAN_BS1_3TQ | 时间段1 |
| BS2 | CAN_BS2_3TQ | 时间段2 |
| AutoBusOff | ENABLE | 自动离线恢复 |
| AutoRetransmission | DISABLE | 禁止自动重传（CAN 发送失败的帧不会自动重试） |

#### 6.3 CAN ID 分配（已确认 2026-06-09）
| ID | 方向 | 帧类型 | 周期 |
|-----|------|--------|------|
| 0x100 | Node A → Bus | 心跳帧 | 200ms |
| 0x101 | Node A → Bus | 传感器数据帧 | 500ms |
| 0x200 | Node B → Bus | 心跳帧 | 200ms |
| 0x201 | Node B → Bus | 传感器数据帧 / ACK 帧 | 500ms / 按需 |
| 0x300 | 任意节点 | 广播命令帧 | 按需 |

> 当前阶段（单节点自测）只用 0x100 和 0x101。Node B 的 ID 在第二阶段启用。

#### 6.4 CAN 接收方式：中断驱动（vs 任务轮询）

> **设计决策：为什么用中断驱动而不是任务轮询？**

| | 中断驱动 ✅ | 任务轮询 ❌ |
|---|---|---|
| **响应延迟** | μs 级（ISR 立即响应硬件） | ms 级（取决于 osDelay 周期） |
| **丢帧风险** | 低（只要 ISR 够短，< 10μs） | 高！osDelay(1ms) 期间可来 7~8 帧，硬件 FIFO 只有 3 级 |
| **CPU 占用** | 极低（只在有帧时触发） | 持续消耗（每次轮询都读 CAN 寄存器） |
| **工业标准** | ✅ 汽车/工业标准做法 | 仅低速场景（< 50Kbps） |

> **关键数据**：500Kbps 下，一帧标准 CAN 约 130μs。任务轮询时 osDelay(1) = 1ms，期间最多来 7 帧，而硬件 FIFO 只有 3 级深度 → 丢 4 帧。

> **ISR 快进快出 + 任务做重活**（标准 FreeRTOS 模式）：
> ```
> ISR:  读硬件 FIFO → 封装 can_msg_t → osMessageQueuePut(超时=0, ISR安全) → 返回 (<10μs)
> 任务: osMessageQueueGet(阻塞) → 协议解析 → 业务处理 → 更新状态
> ```

**中断使用细节**：
- 使用 **CAN1_RX0_IRQn**（FIFO0 消息挂起中断）
- 在 `stm32f1xx_it.c` 的 USER CODE 区域添加 `CAN1_RX0_IRQHandler`
- 中断处理流程：`HAL_CAN_IRQHandler(&hcan)` → 自动调用 `HAL_CAN_RxFifo0MsgPendingCallback()`
- 回调中通过 `osMessageQueuePut(CanMsgQueue, &msg, 0, 0)` 将消息推入队列（timeout=0，ISR 安全）

#### 6.5 CAN 协议帧格式（已确认 2026-06-09）

```c
// 消息结构体（队列传递用，sizeof ≈ 20 字节）
typedef struct {
    uint32_t id;         // CAN ID（标准帧低 11 位有效）
    uint8_t  ide;        // 0=标准帧, 1=扩展帧
    uint8_t  rtr;        // 0=数据帧, 1=远程帧
    uint8_t  dlc;        // 数据长度 (0~8)
    uint8_t  data[8];    // 数据载荷
    uint32_t timestamp;  // 接收时刻 ms
} can_msg_t;

// 帧类型定义（data[0]）
#define CAN_FRAME_TYPE_HEARTBEAT  0x01  // 心跳帧
#define CAN_FRAME_TYPE_SENSOR     0x02  // 传感器帧
#define CAN_FRAME_TYPE_CMD        0x03  // 命令帧
#define CAN_FRAME_TYPE_ACK        0x04  // 应答帧
#define CAN_FRAME_TYPE_EVENT      0x05  // 事件帧

// ---- 心跳帧 (ID=0x100, 每200ms) ----
// [0]=0x01 [1]=系统标志位 [2]=序列号(0~255) [3..7]=保留(0xFF)
//   Data[1] 位定义（位域打包，1字节=8个标志）：
//     bit0: CAN通信正常(1)/异常(0)
//     bit1: 对方在线(1)/离线(0)
//     bit2: 传感器数据就绪(1)/未就绪(0)
//     bit3: 错误告警(1)/正常(0)
//     bit4~7: 保留(0)

// ---- 传感器帧 (ID=0x101, 每500ms) ----
// [0]=0x02 [1]=序列号 [2..3]=模拟值1(uint16_t,大端) [4..5]=模拟值2(uint16_t,大端) [6..7]=tick低16位
//   模拟值1: 起始0，每500ms递增1（模拟传感器读数变化）
//   模拟值2: 起始1000，每500ms递减1（模拟另一路传感器）
//   tick: HAL_GetTick() 低16位，ms

// ---- 命令帧 (ID=0x300, 按需发送) ----
// [0]=0x03 [1]=命令码 [2]=参数 [3..7]=保留

// ---- ACK帧 (ID=0x201, 收到命令后应答) ----
// [0]=0x04 [1]=确认命令码 [2]=结果(0x00=成功,0x01=失败) [3..7]=保留
```

#### 6.6 SN65HVD230 注意事项
- 工作电压 3.3V，与 STM32F103 电平兼容，无需电平转换
- **RS 引脚（第8脚）**：控制工作模式
  - RS = GND：高速模式（≥ 500Kbps 推荐）
  - RS = 通过电阻接地（斜率控制）：降低 EMI，适用于低速
  - RS = VCC：低功耗待机模式
- 如果板上 RS 引脚可配置，建议默认接 GND（高速模式）
- 总线两端必须各接 120Ω 终端电阻

### 7. 注释风格规则（强制执行）
- **所有注释必须使用中文**，包括函数注释、行内注释、模块分隔注释
- **函数注释格式**：每个函数前必须包含以下内容的注释块（`@brief` 和 `@param` 必须，`@note` 和 `@warning` 按需添加）：
  ```c
  /**
    * @brief    【函数功能的一句话描述】
    * @param    参数名    【参数含义 + 取值范围 + 说明】
    * @retval   返回值含义，若无返回值则写"无"
    * @note     【可选】注意事项、使用限制、适用场景
    * @warning  【可选】潜在风险、副作用、禁止操作
    */
  ```
- **行内注释格式**：关键操作必须注释，格式为 `// 操作描述 + 补充说明`
- **代码块分隔注释**：不同功能模块之间用星号分隔行隔开：
  ```c
  /**********************************************************
  ***  CAN 接收处理
  **********************************************************/
  ```
- **注释完整性要求**：
  - 变量定义需注明单位（如 `ms`、`Hz`、`Kbps`）
  - 魔数（magic number）需解释其含义
  - 位操作（`<<` / `>>` / `&`）需注明提取的是哪几位
  - 状态机/条件分支需注释判断意图

---

## 项目分阶段规划（2026-06-08 讨论确认）

### 📦 第一阶段：单节点基础框架（当前）
- [ ] 创建 `can_protocol.h/c` — CAN 协议定义和构造/解析函数
- [ ] 创建 `can_app.h/c` — CAN 应用层（中断回调 + 业务逻辑）
- [ ] 填充 `freertos.c` 三个任务函数体（CanTxTask / CanRxTask / LedTask）
- [ ] 修改 `can.c` — 添加 CAN 启动 + 中断使能
- [ ] 修改 `stm32f1xx_it.c` — 添加 CAN1_RX0_IRQHandler
- [ ] 修改 `main.c` — 添加 printf 重定向到 USART1
- [ ] 实现 CAN 回环自测模式（`CAN_MODE_SILENT_LOOPBACK`）
- [ ] 编译通过 + 串口日志验证

### 🔗 第二阶段：双节点通信
- [ ] Node B 项目（从 Node A 复制，修改 CAN ID 配置）
- [ ] 心跳互检 + 在线/离线判断
- [ ] 传感器数据周期交互 + ACK 应答
- [ ] 命令帧远程控制对方 LED
- [ ] 通信统计（收/发计数、错误计数、心跳丢失次数）

### 🌟 第三阶段：简历亮点（选做）
- [ ] 串口 CLI 调试控制台（`stats` / `send` / `reboot` 命令）
- [ ] CAN 总线错误处理（Bus-Off 检测 + 自动恢复）
- [ ] CANopen 迷你子集（NMT 状态机 + Heartbeat + 简易 SDO）
- [ ] Python PC 监控面板（串口 → 实时 CAN 流量显示）

---

## API 索引

### HAL 库（CAN 相关关键函数）
| 函数 | 说明 |
|------|------|
| `HAL_CAN_Start(&hcan)` | 启动 CAN 模块（退出 INIT 状态） |
| `HAL_CAN_Stop(&hcan)` | 停止 CAN 模块 |
| `HAL_CAN_AddTxMessage(&hcan, &txHeader, data, &txMailbox)` | 发送 CAN 消息（阻塞式） |
| `HAL_CAN_GetTxMailboxesFreeLevel(&hcan)` | 查询空闲发送邮箱数 |
| `HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING)` | 使能 FIFO0 消息中断通知 |
| `HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, data)` | 从 FIFO0 读取接收到的消息 |
| `HAL_CAN_GetError(&hcan)` | 获取 CAN 错误状态 |
| `HAL_CAN_ResetError(&hcan)` | 清除错误计数器 |

### CMSIS-RTOS V2（FreeRTOS 封装，关键 API）
| 函数 | 说明 |
|------|------|
| `osThreadNew(func, arg, attr)` | 创建任务 |
| `osDelay(ms)` | 任务延时（阻塞，让出 CPU） |
| `osMessageQueueNew(msg_count, msg_size, attr)` | 创建消息队列 |
| `osMessageQueuePut(queue, msg_ptr, prio, timeout)` | 发送消息到队列（timeout=0 可在 ISR 中使用） |
| `osMessageQueueGet(queue, msg_ptr, prio_ptr, timeout)` | 从队列接收消息（阻塞或超时） |
| `osKernelInitialize()` | 初始化 FreeRTOS 内核 |
| `osKernelStart()` | 启动调度器（不会返回） |

### 用户自定义 API（待实现）
| 函数 | 文件 | 说明 |
|------|------|------|
| `CAN_Protocol_BuildHeartbeat(id, data, state)` | can_protocol.c | 构造心跳帧 |
| `CAN_Protocol_BuildSensorData(id, data, ...)` | can_protocol.c | 构造传感器数据帧 |
| `CAN_Protocol_GetFrameType(data)` | can_protocol.c | 提取帧类型 |
| `CAN_App_Init()` | can_app.c | 应用层初始化（启动CAN、使能中断） |
| `CAN_App_ProcessRxMsg(msg)` | can_app.c | 处理接收消息（协议分发） |
| `CAN_App_SendPeriodic()` | can_app.c | 周期发送入口 |

---

## 踩坑记录

> 格式：`### 坑N：一句话描述` + 现象 + 原因 + 解决方案 + 日期

### 坑1：STM32F1 CAN 硬件 FIFO 只有 3 级
- **日期**：2026-06-08（方案讨论时识别）
- **现象**：CAN 总线 burst 连续 4+ 帧到达时，即使软件队列（CanMsgQueue = 16 条）够大，硬件端也会丢帧
- **原因**：STM32F103 CAN 外设的 RX FIFO 只有 3 级深度（FIFO0 + FIFO1 各 3 条）。如果 CPU 没有在约 400μs 内及时读取，第 4 帧就会溢出丢失。HAL 回调中打印日志（非常耗时！）会急剧扩大此风险
- **解决方案**：
  1. **ISR 中不做任何耗时操作**（不打印日志、不解析协议），只读 FIFO → 塞队列 → 返回
  2. 确保 CanRxTask 是高优先级，能尽快消费队列
  3. 如需进一步降低风险，可在 CubeMX 中使能 FIFO1 以及 Overflow 中断做监控
- **波及**：所有使用 STM32F103 CAN 的项目都受此限制

### 坑2：CAN 硬件 ACK ≠ 软件层 ACK（协议设计基础）
- **日期**：2026-06-09（方案讨论时识别）
- **现象**：容易误以为 CAN 总线硬件 ACK 能保证接收方正确处理了消息，实际它只保证物理层送达（至少有一个节点在 ACK slot 拉低了总线电平）
- **原因**：CAN 硬件 ACK 只验证 CRC 正确且至少一个节点收到了帧，不验证以下内容：
  - 是哪个节点收到的？（可能是总线上任何节点）
  - 应用层处理成功了吗？（可能硬件 FIFO 里有但软件还没读）
  - 校验和/序列号正确吗？（硬件只校验 CRC，不校验数据内容）
- **解决方案**：在应用层实现**软件 ACK + 超时重发**机制
  ```
  发送方 → 命令帧 → CAN总线 → 接收方处理
  发送方 ← ACK帧（帧类型=0x04，含确认命令码和结果）← 接收方
  200ms 内未收到 ACK → 超时重发（最多 3 次）→ 仍失败则告警
  ```
- **设计原则**：CAN 硬件 ACK = "快递已签收（可能是前台签的）"，软件 ACK = "邮件已读回执（本人确认）"

### 坑3：RTR 遥控帧 ≠ 软件请求/应答
- **日期**：2026-06-09
- **注意**：RTR（Remote Transmission Request）遥控帧在现代 CAN 系统中很少使用。它的语义是"请求对方发送指定 ID 的数据帧"，但 DLC 字段语义混乱（表示期望对方返回多少字节，对方可返回不同长度）。当前主流做法是用**普通数据帧**实现请求-应答：发送一个 Data[0]=请求帧类型 的数据帧，对方返回 Data[0]=应答帧类型 的数据帧

### 坑4：标准帧 vs 扩展帧 ID 切换
- **日期**：2026-06-09
- **要点**：
  - 标准帧 11-bit ID：0x000~0x7FF（2048 个 ID），IDE bit=0
  - 扩展帧 29-bit ID：0x00000000~0x1FFFFFFF（约 5.3 亿个 ID），IDE bit=1
  - STM32F1 HAL 中通过 `CAN_TxHeaderTypeDef.IDE = CAN_ID_STD` 或 `CAN_ID_EXT` 切换
  - 本项目用标准帧（11-bit）即可满足 2 节点需求，但 ID 分配方案应预留扩展空间

### 坑5：CAN 硬件滤波器未配置导致收不到任何帧
- **日期**：2026-06-19（回环自测时发现）
- **现象**：回环模式下 CAN 发送正常（`CAN_App_SendFrame` 返回成功），但 `CanRxTask` 永远收不到任何消息，LED 始终慢闪（对方离线状态），串口只有启动横幅
- **原因**：STM32F1 CAN 外设有 14 个硬件滤波器组（Filter Bank）。`HAL_CAN_Init()` 不会自动配置滤波器——默认所有滤波器关闭，**所有接收到的消息都被硬件直接丢弃**，不进 FIFO、不触发中断。即使回环模式下 CAN 控制器内部 TX→RX 连通，帧经过滤波器时仍然被拦截
- **解决方案**：在 `CAN_App_Init()` 的 `HAL_CAN_Start()` 之后、`HAL_CAN_ActivateNotification()` 之前，调用 `HAL_CAN_ConfigFilter()` 配置至少一个滤波器组。初期调试阶段建议**全通滤波器**（掩码全 0 = 接受所有帧）：
  ```c
  CAN_FilterTypeDef sFilterConfig;
  sFilterConfig.FilterBank = 0;
  sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh = 0x0000;
  sFilterConfig.FilterIdLow = 0x0000;
  sFilterConfig.FilterMaskIdHigh = 0x0000;  // 掩码=0 → 不关心 ID 位
  sFilterConfig.FilterMaskIdLow = 0x0000;
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  sFilterConfig.FilterActivation = ENABLE;
  sFilterConfig.SlaveStartFilterBank = 14;
  HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
  ```
- **调试技巧**：这个问题很难从日志定位（发送成功、中断使能、队列创建都无报错），最有效的排查手段是：
  1. 用调试器在 `HAL_CAN_RxFifo0MsgPendingCallback` 打断点 → 不触发 = 确认问题在硬件滤波层
  2. 读取 CAN 寄存器 `CAN->RF0R`（FIFO0 状态寄存器）的 `FMP0` 位（bit1:0）→ 始终为 0 = 无消息进入 FIFO0
- **波及**：所有 STM32F1 CAN 项目都需要显式配置滤波器

### 参考笔记：CAN 协议帧设计的核心知识（2026-06-09 讨论）

#### 知识 1：序列号（Sequence Number）的四种作用
1. **检测丢帧**：收到 seq=3→5（缺了 4），知道中间丢了一帧
2. **检测重复**：连续收到两个 seq=7，知道有一帧被重传了
3. **判断新鲜度**：seq=2（旧的，忽略）vs seq=200（新的，处理）
4. **计算通信质量**：丢帧数/总发送数 = 丢帧率

> 建议：心跳帧用 1 字节序列号（0~255 循环），Data[2]=序列号

#### 知识 2：模拟传感器数据的几种方案
| 方案 | 面试效果 | 说明 |
|------|---------|------|
| 递增计数器 | 基础 | 简单直观 |
| HAL_GetTick() 低 16 位 | 有变化感 | 体现时间意识 |
| (counter << 8) \| (tick & 0xFF) | 较好 | 组合信息 |
| 读 STM32 内部温度传感器 (ADC17) | ⭐加分 | 真有物理意义 |

> 建议先用计数器仿真，后续接入真实传感器——体现工程迭代思路

#### 知识 3：应用层帧设计的标准模板
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
> 8 字节有限，一帧只做一件事。不要在一帧里混搭多种类型数据

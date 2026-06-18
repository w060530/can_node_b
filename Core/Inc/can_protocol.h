/**
  ******************************************************************************
  * @file    can_protocol.h
  * @brief   CAN 通信协议定义 — 多节点通信 Demo
  * @note    设计原则:
  *          - 标准帧 (11-bit ID)，大端字节序
  *          - 分层协议：帧 ID 层 + 帧类型层 + 应用数据层
  *          - 8 字节有限，一帧只做一件事
  *          - 连续值用整字节大端排列，开关量用位域打包
  ******************************************************************************
  */
#ifndef __CAN_PROTOCOL_H__
#define __CAN_PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**********************************************************
 ***  CAN ID 分配表
 **********************************************************/
/*
 * ┌────────┬──────────┬──────────────────────────────┐
 * │ ID     │ 方向     │ 用途                         │
 * ├────────┼──────────┼──────────────────────────────┤
 * │ 0x100  │ A → Bus  │ Node A 心跳帧 (200ms)        │
 * │ 0x101  │ A → Bus  │ Node A 传感器数据帧 (500ms)  │
 * │ 0x200  │ B → Bus  │ Node B 心跳帧 (200ms)        │
 * │ 0x201  │ B → Bus  │ Node B 传感器数据帧 / ACK帧  │
 * │ 0x300  │ 任意     │ 广播命令帧                   │
 * └────────┴──────────┴──────────────────────────────┘
 */
#define CAN_ID_HEARTBEAT_A      0x100U  /* Node A 心跳帧 */
#define CAN_ID_SENSOR_A         0x101U  /* Node A 传感器数据帧 */
#define CAN_ID_HEARTBEAT_B      0x200U  /* Node B 心跳帧 */
#define CAN_ID_SENSOR_B         0x201U  /* Node B 传感器数据帧 / ACK 应答帧 */
#define CAN_ID_BROADCAST_CMD    0x300U  /* 广播命令帧 */

/**********************************************************
 ***  帧类型定义 (占用 Data[0])
 **********************************************************/
#define CAN_FRAME_HEARTBEAT     0x01U   /* 心跳帧：周期发送，宣告节点存活 */
#define CAN_FRAME_SENSOR        0x02U   /* 传感器数据帧：携带模拟传感器值 */
#define CAN_FRAME_CMD           0x03U   /* 命令帧：控制对方节点执行操作 */
#define CAN_FRAME_ACK           0x04U   /* 应答帧：确认命令执行结果 */
#define CAN_FRAME_EVENT         0x05U   /* 事件通知帧：异步事件上报 */

/**********************************************************
 ***  心跳帧 Data[1] 位域定义（1 字节 = 8 个独立标志）
 **********************************************************/
#define HEARTBEAT_FLAG_CAN_OK       (1U << 0)  /* bit0: CAN 通信正常 */
#define HEARTBEAT_FLAG_PEER_ONLINE  (1U << 1)  /* bit1: 对方在线 */
#define HEARTBEAT_FLAG_SENSOR_OK    (1U << 2)  /* bit2: 传感器就绪 */
#define HEARTBEAT_FLAG_WARNING      (1U << 3)  /* bit3: 错误告警 */
#define HEARTBEAT_FLAG_RESERVED     0xF0U       /* bit4~7: 保留掩码 */

/**********************************************************
 ***  命令码定义
 **********************************************************/
#define CMD_LED_ON                  0x10U   /* 点亮 LED */
#define CMD_LED_OFF                 0x11U   /* 熄灭 LED */
#define CMD_LED_TOGGLE              0x12U   /* LED 翻转 */
#define CMD_REQ_STATUS              0x20U   /* 请求对方上报当前状态 */

/* ACK 结果码 */
#define ACK_RESULT_SUCCESS          0x00U   /* 命令执行成功 */
#define ACK_RESULT_FAIL             0x01U   /* 命令执行失败 */
#define ACK_RESULT_TIMEOUT          0x02U   /* 命令超时（发送方内部使用） */

/**********************************************************
 ***  通信周期参数（单位：ms）
 **********************************************************/
#define HEARTBEAT_PERIOD_MS         200U    /* 心跳帧发送周期 */
#define SENSOR_PERIOD_MS            500U    /* 传感器帧发送周期 */
#define HEARTBEAT_TIMEOUT_MS        1000U   /* 心跳超时：1s 未收到对方心跳 → 判定离线 */
#define LED_TASK_PERIOD_MS          100U    /* LED 状态刷新周期 */

/**********************************************************
 ***  CAN 消息结构体（用于 FreeRTOS 队列传递）
 ***  sizeof(can_msg_t) ≈ 20 字节，小于队列 ItemSize(48 字节)
 **********************************************************/
#pragma pack(push, 1)           /* 按 1 字节对齐，消除填充字节 */
typedef struct {
    uint32_t id;            /* CAN ID（标准帧低 11 位有效）                */
    uint8_t  ide;          /* 标识符扩展：0=标准帧, 1=扩展帧             */
    uint8_t  rtr;          /* 远程请求：0=数据帧, 1=远程帧              */
    uint8_t  dlc;          /* 数据长度 (0~8)                           */
    uint8_t  data[8];      /* 数据载荷                                  */
    uint32_t timestamp;    /* 接收时间戳 (ms)，来自 HAL_GetTick()       */
} can_msg_t;
#pragma pack(pop)              /* 恢复默认对齐 */

/**********************************************************
 ***  系统通信状态结构体（全局共享）
 **********************************************************/
typedef struct {
    /* ---- 发送统计 ---- */
    uint32_t tx_heartbeat_cnt;  /* 心跳帧发送计数              */
    uint32_t tx_sensor_cnt;     /* 传感器帧发送计数            */
    uint32_t tx_error_cnt;      /* 发送失败计数                */

    /* ---- 接收统计 ---- */
    uint32_t rx_heartbeat_cnt;  /* 心跳帧接收计数              */
    uint32_t rx_sensor_cnt;     /* 传感器帧接收计数            */
    uint32_t rx_cmd_cnt;        /* 命令帧接收计数              */
    uint32_t rx_error_cnt;      /* 接收错误计数                */

    /* ---- 通信质量 ---- */
    uint32_t heartbeat_lost;    /* 心跳丢失次数（序列号不连续） */
    uint32_t bus_off_count;     /* CAN Bus-Off 次数            */
    uint32_t last_rx_tick;      /* 最后一次成功接收的时间戳 (ms) */
    uint32_t last_tx_tick;      /* 最后一次成功发送的时间戳 (ms) */

    /* ---- 节点状态 ---- */
    uint8_t  local_state;       /* 本节点状态（位域）           */
    uint8_t  peer_online;       /* 对方节点在线标志：1=在线, 0=离线 */
    uint8_t  heartbeat_seq_tx;  /* 本节点心跳发送序列号 (0~255)  */
    uint8_t  heartbeat_seq_rx;  /* 对方节点上次收到的序列号     */
} sys_comm_status_t;

/**********************************************************
 ***  协议函数声明
 **********************************************************/

/* ---- 帧构造函数（把数据打包成 CAN 帧的 8 字节）---- */
void CAN_Protocol_BuildHeartbeat(uint32_t id, uint8_t *data,
                                  uint8_t flags, uint8_t seq);
void CAN_Protocol_BuildSensor(uint32_t id, uint8_t *data,
                               uint8_t seq, uint16_t val1, uint16_t val2);
void CAN_Protocol_BuildCmd(uint32_t id, uint8_t *data,
                            uint8_t cmd, uint8_t param);
void CAN_Protocol_BuildAck(uint32_t id, uint8_t *data,
                            uint8_t ack_cmd, uint8_t result);

/* ---- 帧解析函数（从 CAN 帧的 8 字节中提取数据）---- */
uint8_t  CAN_Protocol_GetFrameType(const uint8_t *data);
uint8_t  CAN_Protocol_GetHeartbeatFlags(const uint8_t *data);
uint8_t  CAN_Protocol_GetCmdCode(const uint8_t *data);
void     CAN_Protocol_GetSensorValues(const uint8_t *data,
                                       uint16_t *val1, uint16_t *val2,
                                       uint16_t *tick);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_PROTOCOL_H__ */

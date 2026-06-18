/**
  ******************************************************************************
  * @file    can_protocol.c
  * @brief   CAN 协议层实现 — 帧构造与解析函数
  * @note    所有对 Data[8] 的打包/解包逻辑集中在此文件，其他模块不直接操作字节
  ******************************************************************************
  */
#include "can_protocol.h"
#include <string.h>  /* memset */

/**********************************************************
 ***  帧构造函数（打包）
 **********************************************************/

/**
  * @brief   构造心跳帧的 8 字节数据载荷
  * @param   id      未使用（帧 ID 在 HAL 层设置，此处保留接口一致性）
  * @param   data    输出缓冲区（至少 8 字节），接收方通过此指针读取结果
  * @param   flags   系统标志位，按位或组合 HEARTBEAT_FLAG_xxx 宏
  * @param   seq     序列号，0~255 循环递增
  * @retval  无
  * @note    帧格式: [0]=0x01 [1]=flags [2]=seq [3..7]=0xFF
  */
void CAN_Protocol_BuildHeartbeat(uint32_t id, uint8_t *data,
                                  uint8_t flags, uint8_t seq)
{
    (void)id;  /* 当前帧 ID 在 HAL CAN 发送时通过 txHeader.StdId 设置 */

    memset(data, 0xFF, 8);          /* 未使用字节填充 0xFF */

    data[0] = CAN_FRAME_HEARTBEAT;   /* 帧类型：心跳 */
    data[1] = flags;                 /* 系统标志位（位域打包） */
    data[2] = seq;                   /* 序列号，0~255 循环 */
}

/**
  * @brief   构造传感器数据帧的 8 字节数据载荷
  * @param   id      未使用（保留接口一致性）
  * @param   data    输出缓冲区（至少 8 字节）
  * @param   seq     序列号，0~255 循环递增
  * @param   val1    模拟值 1（如模拟温度），uint16_t，大端写入
  * @param   val2    模拟值 2（如模拟湿度），uint16_t，大端写入
  * @retval  无
  * @note    帧格式: [0]=0x02 [1]=seq [2..3]=val1(BE) [4..5]=val2(BE) [6..7]=0x0000
  *          后续 Phase 2 可把 [6..7] 改为 HAL_GetTick() 低 16 位
  */
void CAN_Protocol_BuildSensor(uint32_t id, uint8_t *data,
                               uint8_t seq, uint16_t val1, uint16_t val2)
{
    (void)id;

    memset(data, 0x00, 8);

    data[0] = CAN_FRAME_SENSOR;      /* 帧类型：传感器数据 */
    data[1] = seq;                   /* 序列号 */

    /* 大端写入 val1 → data[2..3] */
    data[2] = (uint8_t)(val1 >> 8);  /* 高字节 */
    data[3] = (uint8_t)(val1 & 0xFF);/* 低字节 */

    /* 大端写入 val2 → data[4..5] */
    data[4] = (uint8_t)(val2 >> 8);  /* 高字节 */
    data[5] = (uint8_t)(val2 & 0xFF);/* 低字节 */

    /* data[6..7] 保留为 0x00，后续可改为 tick 低 16 位 */
}

/**
  * @brief   构造命令帧的 8 字节数据载荷
  * @param   id      未使用
  * @param   data    输出缓冲区（至少 8 字节）
  * @param   cmd     命令码，参见 CMD_xxx 宏定义
  * @param   param   命令参数，含义取决于具体命令
  * @retval  无
  * @note    帧格式: [0]=0x03 [1]=cmd [2]=param [3..7]=0x00
  */
void CAN_Protocol_BuildCmd(uint32_t id, uint8_t *data,
                            uint8_t cmd, uint8_t param)
{
    (void)id;

    memset(data, 0x00, 8);

    data[0] = CAN_FRAME_CMD;         /* 帧类型：命令 */
    data[1] = cmd;                   /* 命令码 */
    data[2] = param;                 /* 命令参数 */
}

/**
  * @brief   构造 ACK 应答帧的 8 字节数据载荷
  * @param   id        未使用
  * @param   data      输出缓冲区（至少 8 字节）
  * @param   ack_cmd   被确认的命令码
  * @param   result    执行结果，参见 ACK_RESULT_xxx
  * @retval  无
  * @note    帧格式: [0]=0x04 [1]=ack_cmd [2]=result [3..7]=0x00
  */
void CAN_Protocol_BuildAck(uint32_t id, uint8_t *data,
                            uint8_t ack_cmd, uint8_t result)
{
    (void)id;

    memset(data, 0x00, 8);

    data[0] = CAN_FRAME_ACK;         /* 帧类型：应答 */
    data[1] = ack_cmd;               /* 确认的命令码（发送方据此匹配请求） */
    data[2] = result;                /* 执行结果：0=成功，1=失败 */
}

/**********************************************************
 ***  帧解析函数（解包）
 **********************************************************/

/**
  * @brief   从 CAN 帧载荷中提取帧类型
  * @param   data    接收到的 CAN 帧数据（至少 8 字节）
  * @retval  帧类型值（CAN_FRAME_HEARTBEAT / CAN_FRAME_SENSOR / ...）
  * @note    只读取 data[0]，不修改数据
  */
uint8_t CAN_Protocol_GetFrameType(const uint8_t *data)
{
    return data[0];
}

/**
  * @brief   从心跳帧中提取系统标志位
  * @param   data    接收到的 CAN 帧数据
  * @retval  标志位字节，按位与 HEARTBEAT_FLAG_xxx 宏判断各标志状态
  * @note    调用方应先确认帧类型为 CAN_FRAME_HEARTBEAT 再调用此函数
  */
uint8_t CAN_Protocol_GetHeartbeatFlags(const uint8_t *data)
{
    return data[1];
}

/**
  * @brief   从命令帧中提取命令码
  * @param   data    接收到的 CAN 帧数据
  * @retval  命令码（CMD_LED_ON / CMD_LED_OFF / ...）
  */
uint8_t CAN_Protocol_GetCmdCode(const uint8_t *data)
{
    return data[1];
}

/**
  * @brief   从传感器数据帧中提取模拟值和 tick
  * @param   data    接收到的 CAN 帧数据
  * @param   val1    输出：模拟值 1（大端 → 主机字节序）
  * @param   val2    输出：模拟值 2
  * @param   tick    输出：系统 tick 低 16 位（当前为保留字段，返回 0）
  * @retval  无
  * @note    大端解码：val1 = data[2]<<8 | data[3]
  */
void CAN_Protocol_GetSensorValues(const uint8_t *data,
                                   uint16_t *val1, uint16_t *val2,
                                   uint16_t *tick)
{
    /* 大端转主机字节序：高字节左移 8 位 | 低字节 */
    *val1 = ((uint16_t)data[2] << 8) | data[3];
    *val2 = ((uint16_t)data[4] << 8) | data[5];
    *tick = ((uint16_t)data[6] << 8) | data[7];   /* 当前为保留字段 */
}

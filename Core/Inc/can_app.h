/**
  ******************************************************************************
  * @file    can_app.h
  * @brief   CAN 应用层接口 — CAN 初始化 + 消息处理
  * @note    应用层负责:
  *          - CAN 外设启动、中断使能
  *          - 接收消息的协议分发（根据帧类型调用不同处理分支）
  *          - 通信状态更新
  *          - 不直接操作 CAN 硬件寄存器，通过 HAL + can_protocol 层操作
  ******************************************************************************
  */
#ifndef __CAN_APP_H__
#define __CAN_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "can_protocol.h"

/**********************************************************
 ***  全局通信状态（单例，所有任务共享访问）
 **********************************************************/
extern sys_comm_status_t g_comm_status;

/**********************************************************
 ***  应用层函数声明
 **********************************************************/

/**
  * @brief   CAN 应用层初始化
  * @note    启动 CAN 外设 → 使能 FIFO0 消息挂起中断通知
  *          应在 FreeRTOS 调度器启动前调用
  * @retval  0=成功，非0=失败
  */
int CAN_App_Init(void);

/**
  * @brief   检测 CAN 总线状态，Bus-Off 时自动恢复
  * @retval  0=正常，-1=Bus-Off 已恢复，-2=恢复失败
  */
int CAN_App_CheckAndRecoverBusOff(void);

/**
  * @brief   每隔 2 秒打印通信状态摘要（需在 CanTxTask 循环中调用）
  */
void CAN_App_PrintStatusIfDue(void);

/**
  * @brief   处理接收到的 CAN 消息（协议分发入口）
  * @param   msg    指向从队列取出的 CAN 消息
  * @retval  无
  * @note    根据 msg->data[0]（帧类型）分发给对应处理分支：
  *          心跳帧 → 更新 g_comm_status.peer_online + 序列号检查
  *          传感器帧 → 提取模拟值 + 更新统计
  *          命令帧 → 执行命令 + 触发 ACK 应答
  *          ACK帧  → 匹配待确认命令 + 更新结果
  */
void CAN_App_ProcessRxMsg(const can_msg_t *msg);

/**
  * @brief   通过 HAL 发送一帧 CAN 消息
  * @param   std_id  标准帧 ID（11 位有效）
  * @param   data    8 字节数据载荷
  * @param   dlc     数据长度 (1~8)
  * @retval  0=发送成功，非0=发送失败
  * @note    封装 HAL_CAN_AddTxMessage，自动处理发送邮箱分配
  */
int CAN_App_SendFrame(uint32_t std_id, const uint8_t *data, uint8_t dlc);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_APP_H__ */

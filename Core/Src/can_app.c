/**
  ******************************************************************************
  * @file    can_app.c
  * @brief   CAN 应用层实现 — CAN 初始化 + 消息处理 + 帧发送
  * @note    依赖:
  *          - HAL CAN 驱动 (hcan)
  *          - can_protocol 协议层（帧构造/解析）
  *          - FreeRTOS 消息队列（在 freertos.c 的回调中引用）
  ******************************************************************************
  */
#include "can_app.h"
#include "can.h"        /* hcan 句柄声明 */
#include "usart.h"      /* huart1 调试串口 */

/* ---- 全局通信状态（单例）---- */
sys_comm_status_t g_comm_status = {0};

/**********************************************************
 ***  CAN 应用层初始化
 **********************************************************/

/**
  * @brief   CAN 应用层初始化：启动 CAN → 使能中断通知
  * @retval  0=成功，-1=启动失败
  */
int CAN_App_Init(void)
{
    /* 启动 CAN 外设（退出 INIT 状态，进入 NORMAL/LOOPBACK 模式） */
    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        return -1;  /* 启动失败，检查 CAN 时钟和引脚配置 */
    }

    /* 配置 CAN 硬件接收滤波器
     * 关键：STM32F1 CAN 默认所有滤波器关闭 → 不配置则收不到任何帧！
     * 此处配置一个滤波器组，接受所有标准帧（11-bit ID），放入 FIFO0 */
    {
        CAN_FilterTypeDef sFilterConfig;
        sFilterConfig.FilterBank = 0;                    /* 使用滤波器组 0 */
        sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK; /* 标识符掩码模式 */
        sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;/* 32 位模式 */
        sFilterConfig.FilterIdHigh = 0x0000;              /* ID 高 16 位：全 0 */
        sFilterConfig.FilterIdLow = 0x0000;               /* ID 低 16 位：全 0 */
        sFilterConfig.FilterMaskIdHigh = 0x0000;          /* 掩码高 16 位：0=不关心（全接受） */
        sFilterConfig.FilterMaskIdLow = 0x0000;           /* 掩码低 16 位：0=不关心（全接受） */
        sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0; /* 匹配的消息存入 FIFO0 */
        sFilterConfig.FilterActivation = ENABLE;           /* 使能此滤波器 */
        sFilterConfig.SlaveStartFilterBank = 14;           /* 主 CAN 用，从 CAN 起始 bank = 14 */

        if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK)
        {
            return -3;  /* 滤波器配置失败 */
        }
    }

    /* 使能 FIFO0 消息挂起中断通知
     * 当 CAN 硬件收到一帧并存入 FIFO0 后，触发 HAL_CAN_RxFifo0MsgPendingCallback 回调 */
    if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        return -2;  /* 中断使能失败 */
    }

    /* 在 NVIC 中使能 CAN1 RX0 中断
     * 优先级 5：刚好在 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 边界
     * 回调中调 osMessageQueuePut(timeout=0) 是 ISR 安全的 */
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);

    /* 初始化通信状态结构体 */
    g_comm_status.local_state = HEARTBEAT_FLAG_CAN_OK;  /* 上电默认：CAN 正常 */
    g_comm_status.peer_online = 0;                     /* 初始：对方离线 */
    g_comm_status.last_rx_tick = 0;
    g_comm_status.last_tx_tick = 0;

    return 0;  /* 初始化成功 */
}

/**********************************************************
 ***  CAN 总线错误检测与自动恢复
 **********************************************************/

/**
  * @brief   检测 CAN 总线状态，Bus-Off 时自动恢复（停止→复位→重启→滤波器→中断）
  * @retval  0=CAN 正常，-1=Bus-Off 已恢复，-2=恢复失败
  * @note    调用时机：
  *          - 每次 CanTxTask 发送前（预防性检查）
  *          - CAN_App_SendFrame 发送失败后（响应式检查）
  * @warning 调用 HAL_CAN_GetError 后需紧跟处理，错误标志需手动清零
  */
int CAN_App_CheckAndRecoverBusOff(void)
{
    uint32_t can_error;

    /* 获取 CAN 错误状态（读取后需手动复位才能清零） */
    can_error = HAL_CAN_GetError(&hcan);

    /* ---- Bus-Off 恢复（TEC > 255，节点已离线）---- */
    if (can_error & HAL_CAN_ERROR_BOF)
    {
        g_comm_status.bus_off_count++;  /* 记录 Bus-Off 事件 */

        /* 停止 CAN（进入 INIT 模式） */
        HAL_CAN_Stop(&hcan);

        /* 复位所有错误计数器（TEC/REC 清零） */
        HAL_CAN_ResetError(&hcan);

        /* 等待总线空闲（确保 11 个隐性位已发送） */
        HAL_Delay(1);

        /* 重启 CAN（退出 INIT，进入 NORMAL 模式） */
        if (HAL_CAN_Start(&hcan) != HAL_OK)
        {
            return -2;
        }

        /* 重新配置硬件滤波器（Stop/Start 后显式重配防丢失） */
        {
            CAN_FilterTypeDef sFilterConfig;
            sFilterConfig.FilterBank = 0;
            sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
            sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
            sFilterConfig.FilterIdHigh = 0x0000;
            sFilterConfig.FilterIdLow = 0x0000;
            sFilterConfig.FilterMaskIdHigh = 0x0000;
            sFilterConfig.FilterMaskIdLow = 0x0000;
            sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
            sFilterConfig.FilterActivation = ENABLE;
            sFilterConfig.SlaveStartFilterBank = 14;
            HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
        }

        /* 重新使能 FIFO0 消息挂起中断 */
        HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);

        return -1;  /* Bus-Off 已恢复 */
    }

    /* ---- Error Passive / Error Warning（错误计数偏高，主动清零防恶化）---- */
    if (can_error & (HAL_CAN_ERROR_EPV | HAL_CAN_ERROR_EWG))
    {
        HAL_CAN_ResetError(&hcan);
    }

    return 0;  /* CAN 正常 */
}

/**********************************************************
 ***  帧发送封装
 **********************************************************/

/**
  * @brief   通过 HAL 发送一帧 CAN 标准数据帧
  * @param   std_id  标准帧 ID（低 11 位有效）
  * @param   data    8 字节数据载荷
  * @param   dlc     数据长度 (1~8)，超出 8 会被 CAN 硬件截断
  * @retval  0=发送成功（已分配邮箱），-1=无空闲邮箱
  */
int CAN_App_SendFrame(uint32_t std_id, const uint8_t *data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef   tx_header;
    uint32_t              tx_mailbox;

    /* 填充发送帧头 */
    tx_header.StdId = std_id;           /* 标准帧 ID */
    tx_header.ExtId = 0;
    tx_header.IDE   = CAN_ID_STD;       /* 标准帧 */
    tx_header.RTR   = CAN_RTR_DATA;     /* 数据帧（非远程帧） */
    tx_header.DLC   = dlc;
    tx_header.TransmitGlobalTime = DISABLE;

    /* HAL 发送：自动分配空闲邮箱，阻塞式等待 */
    if (HAL_CAN_AddTxMessage(&hcan, &tx_header, (uint8_t *)data, &tx_mailbox) != HAL_OK)
    {
        g_comm_status.tx_error_cnt++;
        /* 发送失败 → 检查是否 Bus-Off，尝试自动恢复 */
        CAN_App_CheckAndRecoverBusOff();
        return -1;
    }

    /* 更新最后发送时间戳（帧类型计数由调用方维护） */
    g_comm_status.last_tx_tick = HAL_GetTick();

    return 0;
}

/**********************************************************
 ***  接收消息处理（协议分发入口）
 **********************************************************/

/**
  * @brief   根据帧类型分发处理
  * @param   msg   从 CanMsgQueue 取出的消息
  * @note    在 CanRxTask 中调用，不在 ISR 中调用
  */
void CAN_App_ProcessRxMsg(const can_msg_t *msg)
{
    uint8_t frame_type;

    /* 参数校验 */
    if (msg == NULL)
    {
        return;
    }

    /* 提取帧类型 */
    frame_type = CAN_Protocol_GetFrameType(msg->data);

    /* 按帧类型分发 */
    switch (frame_type)
    {
        case CAN_FRAME_HEARTBEAT:
        {
            /* 收到对方心跳帧 —— 仅记录接收时间
             * 对方是否在线的判断权在 CanTxTask（基于超时）
             * 此处不看对方帧内 PEER_ONLINE 标志，避免循环依赖 */
            g_comm_status.last_rx_tick = msg->timestamp;
            g_comm_status.rx_heartbeat_cnt++;

            /* 序列号连续性检查（丢帧检测） */
            {
                uint8_t seq = msg->data[2];   /* 心跳帧 Data[2] = 序列号 */

                /* 非首次接收时检查序列号是否连续 */
                if (g_comm_status.rx_heartbeat_cnt > 1)
                {
                    uint8_t expected = g_comm_status.heartbeat_seq_rx + 1;

                    if (seq != expected)
                    {
                        /* 序列号不连续 → 丢帧，设置告警标志 */
                        g_comm_status.heartbeat_lost++;
                        g_comm_status.local_state |= HEARTBEAT_FLAG_WARNING;
                    }
                }
                /* 记录本次序列号 */
                g_comm_status.heartbeat_seq_rx = seq;
            }

            break;
        }

        case CAN_FRAME_SENSOR:
        {
            uint16_t val1, val2, tick;

            /* 提取传感器模拟值 */
            CAN_Protocol_GetSensorValues(msg->data, &val1, &val2, &tick);

            g_comm_status.rx_sensor_cnt++;

            /* 后续可在此处添加传感器数据处理逻辑 */

            break;
        }

        case CAN_FRAME_CMD:
        {
            uint8_t cmd = CAN_Protocol_GetCmdCode(msg->data);

            g_comm_status.rx_cmd_cnt++;

            /* 根据命令码执行操作
             * 注意：此处为本地 Demo，直接操作 LED
             *       后续 Phase 2 改为通过标志通知 LedTask */
            switch (cmd)
            {
                case CMD_LED_ON:
                    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
                    break;

                case CMD_LED_OFF:
                    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
                    break;

                case CMD_LED_TOGGLE:
                    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
                    break;

                case CMD_REQ_STATUS:
                    /* 请求状态：后续 Phase 2 通过 CanTxTask 发送状态帧 */
                    break;

                default:
                    /* 未知命令，忽略 */
                    break;
            }

            break;
        }

        case CAN_FRAME_ACK:
        {
            /* ACK 帧处理：后续 Phase 2 实现完整的 ACK 匹配逻辑 */
            break;
        }

        default:
        {
            /* 未知帧类型，记录错误 */
            g_comm_status.rx_error_cnt++;
            break;
        }
    }
}

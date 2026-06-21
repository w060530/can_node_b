/**
  ******************************************************************************
  * @file    can_app.c
  * @brief   CAN 应用层 — 初始化 + 消息处理 + 帧发送 + Bus-Off 恢复 + 调试输出
  * @note    依赖: HAL CAN 驱动 / can_protocol 协议层 / FreeRTOS 消息队列
  *          串口调试输出使用 printf（USART1 重定向）
  ******************************************************************************
  */
#include "can_app.h"
#include "can.h"        /* hcan 句柄声明 */
#include "usart.h"      /* huart1 调试串口 */
#include <stdio.h>      /* printf（USART1 重定向） */

/* ---- 全局通信状态（单例）---- */
sys_comm_status_t g_comm_status = {0};

/* ---- 调试辅助：避免日志刷屏 ---- */
static uint8_t  dbg_peer_was_online = 0;        /* 上次对方在线状态，检测变化用 */
static uint32_t dbg_last_status_print_tick = 0; /* 上次状态打印时间戳 */

/**********************************************************
 ***  CAN 应用层初始化
 **********************************************************/

/**
  * @brief   CAN 应用层初始化：启动 CAN → 配置滤波器 → 使能中断
  * @retval  0=成功，负值=失败（-1启动/-2中断/-3滤波器）
  */
int CAN_App_Init(void)
{
    /* ---- 1. 启动 CAN 外设 ---- */
    printf("[CAN] 启动 CAN 外设...\r\n");
    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        printf("[CAN] ❌ 启动失败！检查 CAN 时钟和引脚配置\r\n");
        return -1;
    }
    printf("[CAN] ✅ 启动成功\r\n");

    /* ---- 2. 配置硬件接收滤波器 ---- */
    printf("[CAN] 配置滤波器...\r\n");
    {
        CAN_FilterTypeDef sFilterConfig;
        sFilterConfig.FilterBank = 0;
        sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
        sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
        sFilterConfig.FilterIdHigh = 0x0000;
        sFilterConfig.FilterIdLow = 0x0000;
        sFilterConfig.FilterMaskIdHigh = 0x0000;   /* 掩码全 0 → 接受所有帧 */
        sFilterConfig.FilterMaskIdLow = 0x0000;
        sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
        sFilterConfig.FilterActivation = ENABLE;
        sFilterConfig.SlaveStartFilterBank = 14;

        if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK)
        {
            printf("[CAN] ❌ 滤波器配置失败！\r\n");
            return -3;
        }
    }
    printf("[CAN] ✅ 滤波器配置成功（全通模式）\r\n");

    /* ---- 3. 使能 FIFO0 消息挂起中断 ---- */
    printf("[CAN] 使能 FIFO0 中断...\r\n");
    if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        printf("[CAN] ❌ 中断使能失败！\r\n");
        return -2;
    }
    printf("[CAN] ✅ 中断使能成功\r\n");

    /* ---- 4. NVIC 配置 ---- */
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    printf("[CAN] ✅ NVIC 配置完成（CAN1_RX0 优先级=5）\r\n");

    /* ---- 5. 初始化通信状态 ---- */
    g_comm_status.local_state = HEARTBEAT_FLAG_CAN_OK;
    g_comm_status.peer_online = 0;
    g_comm_status.last_rx_tick = 0;
    g_comm_status.last_tx_tick = 0;
    dbg_peer_was_online = 0;
    dbg_last_status_print_tick = 0;

    printf("[CAN] 模式 = NORMAL（双节点通信，500Kbps）\r\n");
    printf("[CAN] ====== 初始化完毕，等待 CAN 通信 ======\r\n\r\n");

    return 0;
}

/**********************************************************
 ***  CAN 总线错误检测与自动恢复
 ***  ⚠️ 直接读 CAN1->ESR 硬件寄存器，不依赖 HAL_CAN_GetError()
 ***    因为 HAL_CAN_GetError() 读取的是缓存值，需要中断回调更新
 **********************************************************/

/**
  * @brief   检测 CAN 总线状态（直接读硬件寄存器），Bus-Off 时自动恢复
  * @retval  0=CAN 正常，-1=Bus-Off 已恢复，-2=恢复失败
  */
int CAN_App_CheckAndRecoverBusOff(void)
{
    /* 直接读 CAN 硬件错误状态寄存器
     * CAN_ESR bit0=BOFF, bit1=EWGF, bit2=EPVF, bit4-6=LEC */
    uint32_t esr = CAN1->ESR;

    /* ---- Bus-Off（BOFF=1，TEC > 255）---- */
    if (esr & CAN_ESR_BOFF)
    {
        printf("\r\n[CAN] ⚠️⚠️⚠️ BUS-OFF！ESR=0x%04lX (发送错误计数>255，节点已离线)\r\n", esr);
        g_comm_status.bus_off_count++;

        HAL_CAN_Stop(&hcan);
        HAL_CAN_ResetError(&hcan);
        HAL_Delay(1);   /* 等总线 11 个隐性位 */

        if (HAL_CAN_Start(&hcan) != HAL_OK)
        {
            printf("[CAN] ❌ Bus-Off 恢复失败！\r\n");
            return -2;
        }

        /* 重新配置滤波器 + 中断（防 Stop/Start 后丢失） */
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
        HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);

        printf("[CAN] ✅ Bus-Off 恢复完毕，已重新在线 (第%lu次)\r\n\r\n",
               g_comm_status.bus_off_count);
        return -1;
    }

    /* ---- Error Passive（EPVF=1，TEC > 127）---- */
    if (esr & CAN_ESR_EPVF)
    {
        printf("[CAN] ⚠️ Error Passive！ESR=0x%04lX — 复位错误计数器\r\n", esr);
        HAL_CAN_ResetError(&hcan);
    }
    /* ---- Error Warning（EWGF=1，TEC > 96）---- */
    else if (esr & CAN_ESR_EWGF)
    {
        printf("[CAN] ⚠️ Error Warning！ESR=0x%04lX — 复位错误计数器\r\n", esr);
        HAL_CAN_ResetError(&hcan);
    }

    return 0;  /* CAN 正常 */
}

/**********************************************************
 ***  帧发送封装
 **********************************************************/

/**
  * @brief   通过 HAL 发送一帧 CAN 标准数据帧
  * @param   std_id  标准帧 ID（11 位有效）
  * @param   data    8 字节数据载荷
  * @param   dlc     数据长度 (1~8)
  * @retval  0=发送成功，-1=发送失败
  */
int CAN_App_SendFrame(uint32_t std_id, const uint8_t *data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef   tx_header;
    uint32_t              tx_mailbox;

    tx_header.StdId = std_id;
    tx_header.ExtId = 0;
    tx_header.IDE   = CAN_ID_STD;
    tx_header.RTR   = CAN_RTR_DATA;
    tx_header.DLC   = dlc;
    tx_header.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(&hcan, &tx_header, (uint8_t *)data, &tx_mailbox) != HAL_OK)
    {
        g_comm_status.tx_error_cnt++;

        /* 每20次失败打印一次（避免刷屏），首次必打 */
        if (g_comm_status.tx_error_cnt <= 3
            || g_comm_status.tx_error_cnt % 20 == 0)
        {
            printf("[CAN TX] ❌ 发送失败 ID=0x%03lX 失败次数=%lu ESR=0x%04lX\r\n",
                   std_id, g_comm_status.tx_error_cnt, CAN1->ESR);
        }

        /* 每次失败都检查是否 Bus-Off */
        CAN_App_CheckAndRecoverBusOff();
        return -1;
    }

    g_comm_status.last_tx_tick = HAL_GetTick();
    return 0;
}

/**********************************************************
 ***  接收消息处理（协议分发入口）
 **********************************************************/

/**
  * @brief   根据帧类型分发处理（在 CanRxTask 中调用，不在 ISR 中）
  * @param   msg   从 CanMsgQueue 取出的消息
  */
void CAN_App_ProcessRxMsg(const can_msg_t *msg)
{
    uint8_t frame_type;

    if (msg == NULL) { return; }

    frame_type = CAN_Protocol_GetFrameType(msg->data);

    switch (frame_type)
    {
        case CAN_FRAME_HEARTBEAT:
        {
            uint8_t flags = msg->data[1];
            uint8_t seq   = msg->data[2];

            g_comm_status.last_rx_tick = msg->timestamp;
            g_comm_status.rx_heartbeat_cnt++;

            /* 首次收到 + 前 5 次 + 之后每 10 次打印一行 */
            if (g_comm_status.rx_heartbeat_cnt <= 5
                || g_comm_status.rx_heartbeat_cnt % 10 == 0)
            {
                printf("[CAN RX] 💓 心跳帧 ID=0x%03lX seq=%u flags=0x%02X (第%lu次)\r\n",
                       msg->id, seq, flags, g_comm_status.rx_heartbeat_cnt);
            }

            /* 检测对方在线状态变化 */
            if (flags & HEARTBEAT_FLAG_PEER_ONLINE)
            {
                if (!dbg_peer_was_online)
                {
                    printf("[CAN] 🟢 对方上线！ID=0x%03lX\r\n", msg->id);
                    dbg_peer_was_online = 1;
                }
            }
            else
            {
                if (dbg_peer_was_online)
                {
                    printf("[CAN] 🔴 对方离线！ID=0x%03lX\r\n", msg->id);
                    dbg_peer_was_online = 0;
                }
            }

            /* 序列号连续性检查 */
            if (g_comm_status.rx_heartbeat_cnt > 1)
            {
                uint8_t expected = g_comm_status.heartbeat_seq_rx + 1;
                if (seq != expected)
                {
                    g_comm_status.heartbeat_lost++;
                    g_comm_status.local_state |= HEARTBEAT_FLAG_WARNING;
                    printf("[CAN] ⚠️ 心跳丢帧！期望seq=%u 实际seq=%u 累计丢失=%lu\r\n",
                           expected, seq, g_comm_status.heartbeat_lost);
                }
                else
                {
                    /* 连续收到正确序列号 → 通信已恢复 → 清除告警 */
                    g_comm_status.local_state &= ~HEARTBEAT_FLAG_WARNING;
                }
            }
            g_comm_status.heartbeat_seq_rx = seq;

            break;
        }

        case CAN_FRAME_SENSOR:
        {
            uint16_t val1, val2, tick;
            CAN_Protocol_GetSensorValues(msg->data, &val1, &val2, &tick);
            g_comm_status.rx_sensor_cnt++;

            if (g_comm_status.rx_sensor_cnt <= 3
                || g_comm_status.rx_sensor_cnt % 10 == 0)
            {
                printf("[CAN RX] 📊 传感器帧 ID=0x%03lX val1=%u val2=%u tick=%u (第%lu次)\r\n",
                       msg->id, val1, val2, tick, g_comm_status.rx_sensor_cnt);
            }

            break;
        }

        case CAN_FRAME_CMD:
        {
            uint8_t cmd = CAN_Protocol_GetCmdCode(msg->data);
            g_comm_status.rx_cmd_cnt++;

            printf("[CAN RX] 📋 命令帧 ID=0x%03lX cmd=0x%02X\r\n", msg->id, cmd);

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
                    break;
                default:
                    break;
            }
            break;
        }

        case CAN_FRAME_ACK:
        {
            printf("[CAN RX] ✅ ACK帧 ID=0x%03lX ack_cmd=0x%02X result=0x%02X\r\n",
                   msg->id, msg->data[1], msg->data[2]);
            break;
        }

        default:
        {
            g_comm_status.rx_error_cnt++;
            printf("[CAN RX] ❓ 未知帧类型 type=0x%02X ID=0x%03lX\r\n",
                   frame_type, msg->id);
            break;
        }
    }
}

/**********************************************************
 ***  周期性状态输出（供 CanTxTask 或 LedTask 调用）
 **********************************************************/

/**
  * @brief   每隔约 2 秒打印一次通信状态摘要
  * @note    在 CanTxTask 循环中调用，自动按时间间隔输出
  */
void CAN_App_PrintStatusIfDue(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - dbg_last_status_print_tick) >= 2000)
    {
        dbg_last_status_print_tick = now;

        printf("\r\n===== CAN 状态摘要 (t=%lums) =====\r\n", now);
        printf("  发送: 心跳=%lu 传感器=%lu 失败=%lu\r\n",
               g_comm_status.tx_heartbeat_cnt,
               g_comm_status.tx_sensor_cnt,
               g_comm_status.tx_error_cnt);
        printf("  接收: 心跳=%lu 传感器=%lu 命令=%lu\r\n",
               g_comm_status.rx_heartbeat_cnt,
               g_comm_status.rx_sensor_cnt,
               g_comm_status.rx_cmd_cnt);
        printf("  质量: 丢帧=%lu BusOff=%lu 对方=%s\r\n",
               g_comm_status.heartbeat_lost,
               g_comm_status.bus_off_count,
               g_comm_status.peer_online ? "在线🟢" : "离线🔴");
        printf("  CAN ESR=0x%04lX\r\n", CAN1->ESR);
        printf("====================================\r\n\r\n");
    }
}

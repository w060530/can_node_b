/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "can_protocol.h"
#include "can_app.h"
#include "can.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern sys_comm_status_t g_comm_status;  /* 定义在 can_app.c，所有任务共享 */
/* USER CODE END Variables */
/* Definitions for CanTxTask */
osThreadId_t CanTxTaskHandle;
const osThreadAttr_t CanTxTask_attributes = {
  .name = "CanTxTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for LedTask */
osThreadId_t LedTaskHandle;
const osThreadAttr_t LedTask_attributes = {
  .name = "LedTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for CanRxTask */
osThreadId_t CanRxTaskHandle;
const osThreadAttr_t CanRxTask_attributes = {
  .name = "CanRxTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for CanMsgQueue */
osMessageQueueId_t CanMsgQueueHandle;
const osMessageQueueAttr_t CanMsgQueue_attributes = {
  .name = "CanMsgQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void CanTxTask1(void *argument);
void LedTask1(void *argument);
void CanRxTask1(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of CanMsgQueue */
  CanMsgQueueHandle = osMessageQueueNew (16, 48, &CanMsgQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of CanTxTask */
  CanTxTaskHandle = osThreadNew(CanTxTask1, NULL, &CanTxTask_attributes);

  /* creation of LedTask */
  LedTaskHandle = osThreadNew(LedTask1, NULL, &LedTask_attributes);

  /* creation of CanRxTask */
  CanRxTaskHandle = osThreadNew(CanRxTask1, NULL, &CanRxTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_CanTxTask1 */
/**
  * @brief  Function implementing the CanTxTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_CanTxTask1 */
__weak void CanTxTask1(void *argument)
{
  /* USER CODE BEGIN CanTxTask1 */
  /* Infinite loop */
  for(;;)
  {
    uint8_t data[8];
    uint8_t heartbeat_flags;

    /* 发送前健康检查：防止节点独处时发送失败累积 → Bus-Off */
    CAN_App_CheckAndRecoverBusOff();

    /* ---- 心跳帧：每 200ms 发送一次 ---- */
    {
      /* 组装本节点标志位：CAN 正常 + 检查对方是否在线 */
      heartbeat_flags  = HEARTBEAT_FLAG_CAN_OK;
      heartbeat_flags |= HEARTBEAT_FLAG_SENSOR_OK;  /* Demo 阶段传感器始终就绪 */

      /* 根据心跳超时判定对方是否在线
       * 注意：这是唯一判断对方在线的地方，结果同步到 local_state 供 LedTask 读取 */
      if ((HAL_GetTick() - g_comm_status.last_rx_tick) < HEARTBEAT_TIMEOUT_MS)
      {
        heartbeat_flags |= HEARTBEAT_FLAG_PEER_ONLINE;   /* 告知对方：我收到了你的心跳 */
        g_comm_status.peer_online = 1;
        g_comm_status.local_state |= HEARTBEAT_FLAG_PEER_ONLINE;   /* 同步本地状态 */
      }
      else
      {
        g_comm_status.peer_online = 0;
        g_comm_status.local_state &= ~HEARTBEAT_FLAG_PEER_ONLINE;  /* 同步本地状态 */
      }

      /* 序列号 0~255 循环递增（uint8_t 自然溢出回绕） */
      g_comm_status.heartbeat_seq_tx++;

      /* 构造并发送心跳帧（Node B 使用 ID=0x200） */
      CAN_Protocol_BuildHeartbeat(CAN_ID_HEARTBEAT_B, data,
                                   heartbeat_flags, g_comm_status.heartbeat_seq_tx);
      CAN_App_SendFrame(CAN_ID_HEARTBEAT_B, data, 8);

      /* 帧类型计数 */
      g_comm_status.tx_heartbeat_cnt++;
    }

    /* ---- 传感器帧：每 500ms 发送一次 ---- */
    {
      static uint32_t last_sensor_tick = 0;  /* 上次发送传感器帧的时间戳 */
      static uint16_t sensor_val1 = 0;       /* 模拟值1：递增 */
      static uint16_t sensor_val2 = 1000;    /* 模拟值2：递减 */

      if ((HAL_GetTick() - last_sensor_tick) >= SENSOR_PERIOD_MS)
      {
        last_sensor_tick = HAL_GetTick();

        /* 模拟传感器数据变化 */
        sensor_val1++;    /* 模拟温度：缓慢上升，uint16_t 溢出自动回绕 */
        if (sensor_val2 > 0)
        {
            sensor_val2--;    /* 模拟湿度：缓慢下降，避免下溢 */
        }

        /* 构造并发送传感器帧（Node B 使用 ID=0x201） */
        CAN_Protocol_BuildSensor(CAN_ID_SENSOR_B, data,
                                  g_comm_status.heartbeat_seq_tx,
                                  sensor_val1, sensor_val2);
        CAN_App_SendFrame(CAN_ID_SENSOR_B, data, 8);

        /* 帧类型计数 */
        g_comm_status.tx_sensor_cnt++;
      }
    }

    osDelay(HEARTBEAT_PERIOD_MS);  /* 200ms 循环（心跳周期） */
  }
  /* USER CODE END CanTxTask1 */
}

/* USER CODE BEGIN Header_LedTask1 */
/**
* @brief Function implementing the LedTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_LedTask1 */
__weak void LedTask1(void *argument)
{
  /* USER CODE BEGIN LedTask1 */
  /* Infinite loop */
  for(;;)
  {
    /* 状态机：根据通信状态切换 LED 闪烁模式 */
    if (g_comm_status.bus_off_count > 0)
    {
      /* ---- 错误模式：CAN Bus-Off，LED 常亮 ---- */
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    }
    else if (!(g_comm_status.local_state & HEARTBEAT_FLAG_PEER_ONLINE))
    {
      /* ---- 对方离线：慢闪 500ms ---- */
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      osDelay(500);
    }
    else if (g_comm_status.local_state & HEARTBEAT_FLAG_WARNING)
    {
      /* ---- 告警状态：快闪 200ms ---- */
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      osDelay(200);
    }
    else
    {
      /* ---- 正常状态：心跳闪烁（亮 100ms → 灭 1900ms）---- */
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);  /* 亮 */
      osDelay(100);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);    /* 灭 */
      osDelay(1900);
    }
  }
  /* USER CODE END LedTask1 */
}

/* USER CODE BEGIN Header_CanRxTask1 */
/**
* @brief Function implementing the CanRxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_CanRxTask1 */
__weak void CanRxTask1(void *argument)
{
  /* USER CODE BEGIN CanRxTask1 */
  /* Infinite loop */
  for(;;)
  {
    can_msg_t rx_msg;

    /* 阻塞等待 CAN 消息（osWaitForever = 永远等，不消费 CPU） */
    if (osMessageQueueGet(CanMsgQueueHandle, &rx_msg, NULL, osWaitForever) == osOK)
    {
      /* 交给应用层处理：协议分发 + 业务逻辑 */
      CAN_App_ProcessRxMsg(&rx_msg);
    }
  }
  /* USER CODE END CanRxTask1 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief   CAN FIFO0 消息挂起回调（ISR 上下文）
  * @note    由 HAL_CAN_IRQHandler → HAL 自动调用
  *          ISR 中只做三件事：读 FIFO → 封装 → 塞队列（<10μs）
  *          不做协议解析、不打印日志、不操作耗时外设
  * @warning timeout=0 确保在 ISR 中不会阻塞
  */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx_header;
    can_msg_t           msg;

    /* 从硬件 FIFO0 读取一帧 CAN 消息 */
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, msg.data) != HAL_OK)
    {
        return;  /* 读取失败，静默返回（下次中断重试） */
    }

    /* 封装消息结构体 */
    msg.id        = rx_header.StdId;          /* 标准帧 ID */
    msg.ide       = rx_header.IDE;            /* 0=标准帧 */
    msg.rtr       = rx_header.RTR;            /* 0=数据帧 */
    msg.dlc       = rx_header.DLC;            /* 数据长度 */
    msg.timestamp = HAL_GetTick();            /* 接收时间戳 (ms) */

    /* 发送到 FreeRTOS 消息队列
     * timeout=0 → ISR 安全，队列满时立即返回，不阻塞 */
    osMessageQueuePut(CanMsgQueueHandle, &msg, 0, 0);
}

/* USER CODE END Application */


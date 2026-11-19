/**
 ******************************************************************************
 * @file    vs1003.h
 * @brief   VS1003 播放器 (STM32 标准外设库 / 库函数版)
 *          SD 读数据 + SPI-DMA 喂 VS1003, 双缓冲实现"读卡与喂数据并行"
 ******************************************************************************
 */

#ifndef __VS1003_H
#define __VS1003_H

#include "stm32f10x.h"

/* ============================ 硬件引脚定义 ============================ */
/* DREQ 接 PB0, 用 EXTI0 上升沿中断. 换引脚时改下面这一组即可 */
#define VS1003_DREQ_PORT          GPIOB
#define VS1003_DREQ_PIN           GPIO_Pin_0
#define VS1003_DREQ_PORT_SOURCE   GPIO_PortSourceGPIOB
#define VS1003_DREQ_PIN_SOURCE    GPIO_PinSource0
#define VS1003_DREQ_EXTI_LINE     EXTI_Line0
#define VS1003_DREQ_IRQn          EXTI0_IRQn

/* VS1003 控制脚. 播放数据时 XDCS 全程拉低 */
#define VS1003_XDCS_PORT          GPIOA
#define VS1003_XDCS_PIN           GPIO_Pin_4
#define VS1003_XCS_PORT           GPIOA
#define VS1003_XCS_PIN            GPIO_Pin_8
#define VS1003_XRESET_PORT        GPIOE
#define VS1003_XRESET_PIN         GPIO_Pin_11

/* ============================ 缓冲配置 ============================ */
/* 每个缓冲 4096 字节: 128kbps(16KB/s) 可撑 250ms, 够覆盖 SD 读卡延时 */
#define AUDIO_BUF_SIZE            2048

/* ============================ 对外接口 ============================ */
/* 初始化 GPIO / SPI / DREQ-EXTI / SPI-DMA */
void VS1003_Init(void);

/* 主循环调用: 取一个空闲缓冲用于填充; 无空闲返回 NULL */
uint8_t* VS1003_GetFillBuffer(void);

/* 主循环调用: 把填好数据的缓冲提交出去, len 为实际字节数 */
void VS1003_CommitBuffer(uint16_t len);

void VS1003_Play(const char *filename);

#endif /* __VS1003_H */

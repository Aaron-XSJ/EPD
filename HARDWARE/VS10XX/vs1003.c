/**
 ******************************************************************************
 * @file    vs1003.c
 * @brief   VS1003 播放器 (STM32 标准外设库 / 库函数版)
 *
 * 并行原理:
 *   - "喂 VS1003" 完全交给 SPI-DMA + DREQ 中断自动完成, 不占用主循环;
 *   - "读 SD 卡" 放在主循环里阻塞式完成;
 *   - 两者通过 2 块缓冲解耦, 因此在读卡的同时 DMA 仍在喂数据, 真正并行.
 *
 * 目标芯片: STM32F103 @72MHz (SPI1 接 VS1003, DMA1 通道3)
 ******************************************************************************
 */
#include <stddef.h>
#include "vs1003.h"
#include "vs10xx.h"
#include "ff.h"

/* ============================ 内部缓冲与状态 ============================ */
#define NUM_BUFS        2
#define SEND_CHUNK      32                  /* VS1003 单次最多接收 32 字节 */

static uint8_t  audio_buf[NUM_BUFS][AUDIO_BUF_SIZE];
static volatile uint16_t buf_len[NUM_BUFS];
static volatile uint8_t  buf_ready[NUM_BUFS] = {0, 0};

static volatile uint8_t  fill_idx = 0;      /* 生产者(主循环)正在填写的缓冲下标 */
static volatile uint8_t  cons_idx = 0;      /* 消费者(中断)正在发送的缓冲下标 */
static volatile uint16_t cons_ptr = 0;      /* 消费者在缓冲内的读偏移 */
static volatile uint8_t  spi_busy = 0;      /* SPI-DMA 发送忙标志 */

/* ============================ 前置声明 ============================ */
static void VS1003_GPIO_Init(void);
static void VS1003_SPI_Init(void);
static void VS1003_DREQ_EXTI_Init(void);
static void VS1003_SPI_DMA_Init(void);
static void VS1003_KickTx(void);

/* ============================ 对外接口 ============================ */

/**
 * @brief  初始化整套播放通道
 */
void VS1003_Init(void)
{
    VS1003_GPIO_Init();
    VS1003_SPI_Init();
    VS1003_DREQ_EXTI_Init();
    VS1003_SPI_DMA_Init();

    /* 播放数据期间 XDCS 一直拉低, 不要再频繁切换 */
    GPIO_ResetBits(VS1003_XDCS_PORT, VS1003_XDCS_PIN);
}

/**
 * @brief  取一个空闲缓冲给主循环填写(读 SD 卡)
 * @retval 缓冲首地址; 若无空闲缓冲返回 0
 */
uint8_t* VS1003_GetFillBuffer(void)
{
    if (buf_ready[fill_idx]) {
        return 0;                    /* 该缓冲还没被消费完, 先别填 */
    }
    return audio_buf[fill_idx];
}

/**
 * @brief  提交已填好数据的缓冲
 * @param  len 实际写入的字节数
 */
void VS1003_CommitBuffer(uint16_t len)
{
    if (len == 0) {
        return;
    }

    __disable_irq();
    buf_len[fill_idx]   = len;
    buf_ready[fill_idx] = 1;
    fill_idx ^= 1;
    VS1003_KickTx();                 /* 若此刻 DREQ 已为高, 立即开始发送 */
    __enable_irq();
}

/* ============================ 内部实现 ============================ */

/**
 * @brief  控制脚 GPIO 初始化 (XDCS / XCS / XRESET)
 */
static void VS1003_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOE, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = VS1003_XDCS_PIN | VS1003_XCS_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(VS1003_XDCS_PORT, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin   = VS1003_XRESET_PIN;
	GPIO_Init(VS1003_XRESET_PORT, &GPIO_InitStructure);

    /* 默认: 片选无效(高), 复位释放(高) */
    GPIO_SetBits(VS1003_XDCS_PORT,  VS1003_XDCS_PIN | VS1003_XCS_PIN);
    GPIO_SetBits(VS1003_XRESET_PORT, VS1003_XRESET_PIN);
}

/**
 * @brief  SPI1 初始化 (连接 VS1003)
 */
static void VS1003_SPI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef  SPI_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    /* SCK=PA5, MOSI=PA7: 复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* MISO=PA6: 浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI1, &SPI_InitStructure);

    SPI_Cmd(SPI1, ENABLE);
}

/**
 * @brief  DREQ 外部中断初始化 (上升沿, 最高优先级)
 */
static void VS1003_DREQ_EXTI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    /* DREQ 输入, 下拉防悬空误触发 */
    GPIO_InitStructure.GPIO_Pin  = VS1003_DREQ_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(VS1003_DREQ_PORT, &GPIO_InitStructure);

    GPIO_EXTILineConfig(VS1003_DREQ_PORT_SOURCE, VS1003_DREQ_PIN_SOURCE);

    EXTI_InitStructure.EXTI_Line    = VS1003_DREQ_EXTI_LINE;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = VS1003_DREQ_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;      /* 最高 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/**
 * @brief  SPI1_TX 的 DMA 初始化 (DMA1 通道3)
 */
static void VS1003_SPI_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA1_Channel3);                                   /* SPI1_TX 固定用 DMA1_Ch3 */
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)audio_buf[0];
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize         = SEND_CHUNK;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High; /* 仅次于 DREQ 中断 */
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    /* 使能 SPI1 的发送 DMA 请求 */
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

    /* 传输完成中断 */
    DMA_ITConfig(DMA1_Channel3, DMA_IT_TC, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Channel3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;      /* 次高 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/**
 * @brief  启动一次 32 字节的 DMA 发送
 *         (DREQ 为高且当前缓冲有数据时才真正发送)
 */
static void VS1003_KickTx(void)
{
    uint16_t remain;
    uint16_t len;

    if (spi_busy)                         return;
    if (!buf_ready[cons_idx])             return;
    if (cons_ptr >= buf_len[cons_idx])    return;

    remain = buf_len[cons_idx] - cons_ptr;
    len    = (remain > SEND_CHUNK) ? SEND_CHUNK : remain;

    spi_busy = 1;

    DMA_Cmd(DMA1_Channel3, DISABLE);
//    while (DMA_GetCmdStatus(DMA1_Channel3) == ENABLE);   /* 等通道真正关闭 */
	while(DMA1_Channel3->CCR & 0x0001){}
	
    DMA1_Channel3->CMAR = (uint32_t)&audio_buf[cons_idx][cons_ptr];
    DMA_SetCurrDataCounter(DMA1_Channel3, len);
    DMA_ClearFlag(DMA1_FLAG_TC3 | DMA1_FLAG_GL3);
    DMA_Cmd(DMA1_Channel3, ENABLE);
}

void VS1003_Play(const char *filename)
{
	FIL file;
	UINT     br;
    uint8_t *p;

//	VS_HD_Reset();
//	VS_Soft_Reset();
	
	f_open(&file, filename, FA_READ);
	
	while (1) {
        p = VS1003_GetFillBuffer();
        if (p != NULL) {
            f_read(&file, p, AUDIO_BUF_SIZE, &br);
            if (br == 0) {
                break;                                  /* 文件读完 */
            }
            VS1003_CommitBuffer((uint16_t)br);
        }
    }
	f_close(&file);
	
//	VS_HD_Reset();
//	VS_Soft_Reset();
}

/* ============================ 中断服务函数 ============================ */

/**
 * @brief  DREQ 上升沿中断: VS1003 缓冲区有空位了
 */
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(VS1003_DREQ_EXTI_LINE) != RESET) {
        EXTI_ClearITPendingBit(VS1003_DREQ_EXTI_LINE);
        VS1003_KickTx();
    }
}

/**
 * @brief  SPI1_TX DMA 传输完成中断: 32 字节已发完
 */
void DMA1_Channel3_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC3) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_TC3);
        DMA_Cmd(DMA1_Channel3, DISABLE);
        spi_busy = 0;

        cons_ptr += SEND_CHUNK;
        if (cons_ptr >= buf_len[cons_idx]) {
            /* 当前缓冲发完, 释放给生产者 */
            cons_ptr = 0;
            buf_ready[cons_idx] = 0;
            cons_idx ^= 1;
        }

        /* DREQ 仍为高就接着发, 不依赖下一次上升沿 */
        if (GPIO_ReadInputDataBit(VS1003_DREQ_PORT, VS1003_DREQ_PIN) == Bit_SET) {
            VS1003_KickTx();
        }
    }
}

#include "py32f0xx_hal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SYNTH_SAMPLE_RATE 31250
#define SYNTH_EXTRA_SHIFT 2
#include "../../misc/synth/canta_synth.h"

#define min(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define max(_a, _b) ((_a) > (_b) ? (_a) : (_b))

#ifndef RELEASE
static uint8_t swv_buf[256];
static size_t swv_buf_ptr = 0;
__attribute__ ((noinline, used))
void swv_trap_line()
{
  *(volatile char *)swv_buf;
}
static inline void swv_putchar(uint8_t c)
{
  if (c == '\n') {
    swv_buf[swv_buf_ptr >= sizeof swv_buf ?
      (sizeof swv_buf - 1) : swv_buf_ptr] = '\0';
    swv_trap_line();
    swv_buf_ptr = 0;
  } else if (++swv_buf_ptr <= sizeof swv_buf) {
    swv_buf[swv_buf_ptr - 1] = c;
  }
}
static void swv_printf(const char *restrict fmt, ...)
{
  char s[256];
  va_list args;
  va_start(args, fmt);
  int r = vsnprintf(s, sizeof s, fmt, args);
  for (int i = 0; i < r && i < sizeof s - 1; i++) swv_putchar(s[i]);
  if (r >= sizeof s) {
    for (int i = 0; i < 3; i++) swv_putchar('.');
    swv_putchar('\n');
  }
}
#else
#define swv_printf(...)
#endif

SPI_HandleTypeDef spi1;
DMA_HandleTypeDef dma1_ch1;
TIM_HandleTypeDef tim1;

bool touch_active = false;

#pragma GCC optimize("O3")
static inline void cap_sense()
{
  struct record_t {
    uint16_t t;
    uint16_t v;
  } record[16];

  uint16_t cap[12] = { 0 };
  uint16_t cap_sum[12] = { 0 };

  static const uint16_t MASK[12] = {
    1 <<  8,
    1 <<  0,
    1 <<  2,
    1 <<  3,
    1 <<  4,
    1 <<  5,
    1 <<  6,
    1 <<  7,
    1 << 12,
    1 << 13,
    1 << 10,
    1 << 14,
  };

  for (int its = 0; its < 5; its++) {
    int n_records;
    uint16_t last_v;

    n_records = 0;
    last_v = ~0x15fd;
    record[n_records] = (struct record_t){.t = (uint16_t)-1, .v = last_v};
    __disable_irq();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 1);
    for (int i = 0; i < 200; i++) {
      uint32_t a = GPIOA->IDR;
      uint32_t f = GPIOF->IDR;
      uint16_t cur_v = (a | (f << 6)) | last_v;
      if (last_v != cur_v) n_records++;
      record[n_records] = (struct record_t){.t = i, .v = cur_v};
      last_v = cur_v;
    }
    __enable_irq();
    for (int j = 0; j < 12; j++) cap[j] = 0xffff;
    for (int i = 1; i <= n_records; i++) {
      uint16_t t = record[i - 1].t + 1;
      uint16_t diff = record[i - 1].v ^ record[i].v;
      for (int j = 0; j < 12; j++)
        if (diff & MASK[j]) cap[j] = t;
    }
    for (int j = 0; j < 12; j++)
      if (cap_sum[j] == 0xffff || cap[j] == 0xffff) cap_sum[j] = 0xffff;
      else cap_sum[j] += cap[j];
    for (volatile int j = 0; j < 2000; j++) { }

    // XXX: DRY?
    n_records = 0;
    last_v = 0x15fd;
    record[n_records] = (struct record_t){.t = (uint16_t)-1, .v = last_v};
    __disable_irq();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 0);
    for (int i = 0; i < 200; i++) {
      uint32_t a = GPIOA->IDR;
      uint32_t f = GPIOF->IDR;
      uint16_t cur_v = (a | (f << 6)) & last_v;
      if (last_v != cur_v) n_records++;
      record[n_records] = (struct record_t){.t = i, .v = cur_v};
      last_v = cur_v;
    }
    __enable_irq();
    for (int j = 0; j < 12; j++) cap[j] = 0xffff;
    for (int i = 1; i <= n_records; i++) {
      uint16_t t = record[i - 1].t + 1;
      uint16_t diff = record[i - 1].v ^ record[i].v;
      for (int j = 0; j < 12; j++)
        if (diff & MASK[j]) cap[j] = t;
    }
    for (int j = 0; j < 12; j++)
      if (cap_sum[j] == 0xffff || cap[j] == 0xffff) cap_sum[j] = 0xffff;
      else cap_sum[j] += cap[j];
    for (volatile int j = 0; j < 2000; j++) { }
  }

  // for (int j = 0; j < 12; j++) swv_printf("%3d%c", min(999, cap_sum[j]), j == 11 ? '\n' : ' ');
  __disable_irq();
  touch_active = false;
  for (int j = 0; j < 8; j++) if (cap_sum[j] > 200) touch_active = true;
  bool btns[12] = { false };
  for (int j = 0; j < 12; j++) btns[j] = (cap_sum[j] > 200);
  btns[9] = btns[11] = false;
  synth_buttons(btns);
  __enable_irq();
  // for (int j = 0; j < 12; j++) swv_printf("%d%c", (int)btns[j], j == 11 ? '\n' : ' ');
}

uint16_t audio_buf[256] = { 0 };
#define AUDIO_BUF_HALF_SIZE ((sizeof audio_buf / sizeof audio_buf[0]) / 2)
static const uint16_t sine_table[256] = {
  // python3 -c "from math import sin, pi; print(', '.join('%d' % round(32767.0 * 0.01 * sin(i / 64.0 * pi * 2)) for i in range(256)))"
  0, 32, 64, 95, 125, 154, 182, 208, 232, 253, 272, 289, 303, 314, 321, 326, 328, 326, 321, 314, 303, 289, 272, 253, 232, 208, 182, 154, 125, 95, 64, 32, 0, -32, -64, -95, -125, -154, -182, -208, -232, -253, -272, -289, -303, -314, -321, -326, -328, -326, -321, -314, -303, -289, -272, -253, -232, -208, -182, -154, -125, -95, -64, -32, 0, 32, 64, 95, 125, 154, 182, 208, 232, 253, 272, 289, 303, 314, 321, 326, 328, 326, 321, 314, 303, 289, 272, 253, 232, 208, 182, 154, 125, 95, 64, 32, 0, -32, -64, -95, -125, -154, -182, -208, -232, -253, -272, -289, -303, -314, -321, -326, -328, -326, -321, -314, -303, -289, -272, -253, -232, -208, -182, -154, -125, -95, -64, -32, 0, 32, 64, 95, 125, 154, 182, 208, 232, 253, 272, 289, 303, 314, 321, 326, 328, 326, 321, 314, 303, 289, 272, 253, 232, 208, 182, 154, 125, 95, 64, 32, 0, -32, -64, -95, -125, -154, -182, -208, -232, -253, -272, -289, -303, -314, -321, -326, -328, -326, -321, -314, -303, -289, -272, -253, -232, -208, -182, -154, -125, -95, -64, -32, 0, 32, 64, 95, 125, 154, 182, 208, 232, 253, 272, 289, 303, 314, 321, 326, 328, 326, 321, 314, 303, 289, 272, 253, 232, 208, 182, 154, 125, 95, 64, 32, 0, -32, -64, -95, -125, -154, -182, -208, -232, -253, -272, -289, -303, -314, -321, -326, -328, -326, -321, -314, -303, -289, -272, -253, -232, -208, -182, -154, -125, -95, -64, -32
  // 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
  // 0,
};

// XXX: Simply this!
HAL_StatusTypeDef my_HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size)
{
  HAL_StatusTypeDef errorcode = HAL_OK;

  /* Check tx dma handle */
  assert_param(IS_SPI_DMA_HANDLE(hspi->hdmatx));

  /* Check Direction parameter */
  assert_param(IS_SPI_DIRECTION_2LINES_OR_1LINE(hspi->Init.Direction));

  /* Process Locked */
  __HAL_LOCK(hspi);

  if (hspi->State != HAL_SPI_STATE_READY)
  {
    errorcode = HAL_BUSY;
    goto error;
  }

  if ((pData == NULL) || (Size == 0U))
  {
    errorcode = HAL_ERROR;
    goto error;
  }

  /* Disable SPI peripheral */
  __HAL_SPI_DISABLE(hspi);

  /* Set the transaction information */
  hspi->State       = HAL_SPI_STATE_BUSY_TX;
  hspi->ErrorCode   = HAL_SPI_ERROR_NONE;
  hspi->pTxBuffPtr  = (uint8_t *)pData;
  hspi->TxXferSize  = Size;
  hspi->TxXferCount = Size;

  /* Init field not used in handle to zero */
  hspi->pRxBuffPtr  = (uint8_t *)NULL;
  hspi->TxISR       = NULL;
  hspi->RxISR       = NULL;
  hspi->RxXferSize  = 0U;
  hspi->RxXferCount = 0U;

  /* Configure communication direction : 1Line */
  if (hspi->Init.Direction == SPI_DIRECTION_1LINE)
  {
    SPI_1LINE_TX(hspi);
  }

  /* Set the SPI TxDMA Half transfer complete callback */
  hspi->hdmatx->XferHalfCpltCallback = HAL_SPI_TxHalfCpltCallback;

  /* Set the SPI TxDMA transfer complete callback */
  hspi->hdmatx->XferCpltCallback = HAL_SPI_TxCpltCallback;

  /* Set the DMA error callback */
  hspi->hdmatx->XferErrorCallback = NULL; /* XXX: Modified */

  /* Set the DMA AbortCpltCallback */
  hspi->hdmatx->XferAbortCallback = NULL;

  CLEAR_BIT(hspi->Instance->CR2, SPI_CR2_LDMA_TX);
  /* Packing mode is enabled only if the DMA setting is HALWORD */
  if ((hspi->Init.DataSize <= SPI_DATASIZE_8BIT) && (hspi->hdmatx->Init.MemDataAlignment == DMA_MDATAALIGN_HALFWORD))
  {
    /* Check the even/odd of the data size if enabled */
    if ((hspi->TxXferCount & 0x1U) == 0U)
    {
      CLEAR_BIT(hspi->Instance->CR2, SPI_CR2_LDMA_TX);
      hspi->TxXferCount = (hspi->TxXferCount >> 1U);
    }
    else
    {
      SET_BIT(hspi->Instance->CR2, SPI_CR2_LDMA_TX);
      hspi->TxXferCount = (hspi->TxXferCount >> 1U) + 1U;
    }
  }

  /* Enable the Tx DMA Stream/Channel */
  if (HAL_OK != HAL_DMA_Start_IT(hspi->hdmatx, (uint32_t)hspi->pTxBuffPtr, (uint32_t)&hspi->Instance->DR,
                                 hspi->TxXferCount))
  {
    /* Update SPI error code */
    SET_BIT(hspi->ErrorCode, HAL_SPI_ERROR_DMA);
    errorcode = HAL_ERROR;

    hspi->State = HAL_SPI_STATE_READY;
    goto error;
  }

  /* Check if the SPI is already enabled */
  if ((hspi->Instance->CR1 & SPI_CR1_SPE) != SPI_CR1_SPE)
  {
    /* Enable SPI peripheral */
    __HAL_SPI_ENABLE(hspi);
  }

  /* Enable the SPI Error Interrupt Bit */
  __HAL_SPI_ENABLE_IT(hspi, (SPI_IT_ERR));

  /* XXX: Modified */

error :
  /* Process Unlocked */
  __HAL_UNLOCK(hspi);
  return errorcode;
}

int main(void)
{
  HAL_Init();

  // ======== Clocks ========
  RCC_OscInitTypeDef osc_init = { 0 };
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc_init.HSEState = RCC_HSE_ON;
  osc_init.HSEFreq = RCC_HSE_16_32MHz;
  HAL_RCC_OscConfig(&osc_init);

  RCC_ClkInitTypeDef clk_init = { 0 };
  clk_init.ClockType =
    RCC_CLOCKTYPE_SYSCLK |
    RCC_CLOCKTYPE_HCLK |
    RCC_CLOCKTYPE_PCLK1;
  clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_HSE; // 32 MHz
  clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_1);

  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

  // ======== Option byte ========
  FLASH_OBProgramInitTypeDef ob_prog;
  HAL_FLASH_OBGetConfig(&ob_prog);
  if (!(ob_prog.USERConfig & FLASH_OPTR_NRST_MODE)) {
    ob_prog.USERConfig |= FLASH_OPTR_NRST_MODE;
    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();
    HAL_FLASH_OBProgram(&ob_prog);
    HAL_FLASH_OB_Lock();
    HAL_FLASH_OB_Launch();  // System reset
  }
  // FLASH->OPTR |= FLASH_OPTR_NRST_MODE;  // Cannot be modified after POR?

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  GPIO_InitTypeDef gpio_init;

  // ======== Capacitive touch sensing electrodes ========
  // BTN_OUT
  gpio_init = (GPIO_InitTypeDef){
    .Pin = GPIO_PIN_7,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
  };
  HAL_GPIO_Init(GPIOB, &gpio_init);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 0);

  // BTN_xx (xx = 01, .., 12)
  gpio_init = (GPIO_InitTypeDef){
    .Mode = GPIO_MODE_INPUT,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
  };
  gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
                  GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOA, &gpio_init);
  gpio_init.Pin = GPIO_PIN_2 | GPIO_PIN_4;
  HAL_GPIO_Init(GPIOF, &gpio_init);

  // ======== SPI ========
  gpio_init = (GPIO_InitTypeDef){
    .Mode = GPIO_MODE_AF_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
    .Alternate = GPIO_AF0_SPI1,
  };
  gpio_init.Pin = GPIO_PIN_1; HAL_GPIO_Init(GPIOA, &gpio_init);
  gpio_init.Pin = GPIO_PIN_5; HAL_GPIO_Init(GPIOB, &gpio_init);

  __HAL_RCC_SPI1_CLK_ENABLE();
  spi1 = (SPI_HandleTypeDef){
    .Instance = SPI1,
    .Init = {
      .Mode = SPI_MODE_MASTER,
      .Direction = SPI_DIRECTION_2LINES,
      .DataSize = SPI_DATASIZE_16BIT,
      .CLKPolarity = SPI_POLARITY_LOW,  // CPOL = 0
      .CLKPhase = SPI_PHASE_1EDGE,      // CPHA = 0
      .NSS = SPI_NSS_SOFT,
      .BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32,
        // 1 MHz bitrate = 31.25 kHz @ 16b stereo
      .FirstBit = SPI_FIRSTBIT_MSB,
    },
  };
  HAL_SPI_Init(&spi1);

  // ======== DMA ========
  __HAL_RCC_DMA_CLK_ENABLE();
  dma1_ch1 = (DMA_HandleTypeDef){
    .Instance = DMA1_Channel1,
    .Init = {
      .Direction = DMA_MEMORY_TO_PERIPH,
      .PeriphInc = DMA_PINC_DISABLE,
      .MemInc = DMA_MINC_ENABLE,
      .PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD,
      .MemDataAlignment = DMA_MDATAALIGN_HALFWORD,
      .Mode = DMA_CIRCULAR,
      .Priority = DMA_PRIORITY_MEDIUM,
    },
  };
  HAL_DMA_Init(&dma1_ch1);
  HAL_DMA_ChannelMap(&dma1_ch1, DMA_CHANNEL_MAP_SPI1_TX);
  __HAL_LINKDMA(&spi1, hdmatx, dma1_ch1);

  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 15, 1);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  HAL_NVIC_SetPriority(SPI1_IRQn, 15, 2);
  HAL_NVIC_EnableIRQ(SPI1_IRQn);

  // ======== Timer ========
  gpio_init = (GPIO_InitTypeDef){
    .Pin = GPIO_PIN_6,
    .Mode = GPIO_MODE_AF_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
    .Alternate = GPIO_AF1_TIM1,
  };
  HAL_GPIO_Init(GPIOB, &gpio_init);

  __HAL_RCC_TIM1_CLK_ENABLE();
  tim1 = (TIM_HandleTypeDef){
    .Instance = TIM1,
    .Init = {
      .Prescaler = 31,  // Synchronise with SPI bit clock
      .CounterMode = TIM_COUNTERMODE_DOWN,
      .Period = 31,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
    },
  };
  HAL_TIM_PWM_Init(&tim1);
  HAL_TIM_PWM_ConfigChannel(&tim1, &(TIM_OC_InitTypeDef){
    .OCMode = TIM_OCMODE_PWM1,
    .Pulse = 15,
    .OCPolarity = TIM_OCPOLARITY_HIGH,
    .OCNPolarity = TIM_OCNPOLARITY_HIGH,
    .OCFastMode = TIM_OCFAST_DISABLE,
    .OCIdleState = TIM_OCIDLESTATE_RESET,
    .OCNIdleState = TIM_OCNIDLESTATE_RESET,
  }, TIM_CHANNEL_3);
  // HAL_TIM_PWM_Start(&tim1, TIM_CHANNEL_3);
  __HAL_TIM_MOE_ENABLE(&tim1);
  __HAL_TIM_ENABLE(&tim1);
  // TIM1->CCER |= (TIM_CCx_ENABLE << TIM_CHANNEL_3);

#define abs(_i) ((_i) > 0 ? (_i) : (_i))
  __HAL_SPI_ENABLE(&spi1);
  TIM1->CCER |= (TIM_CCx_ENABLE << TIM_CHANNEL_3);

  // NOTE: This is number of transfers that gets written to DMA_CNDTRx,
  // but reference manual says CNDTR (NDT) is number of bytes?
  // Is datasheet copied from STM32F103 but PY32 actually follows STM32G0x0?
  my_HAL_SPI_Transmit_DMA(&spi1, (uint8_t *)audio_buf, sizeof audio_buf / sizeof audio_buf[0]);

  uint32_t cr2 = SPI1->CR2 | SPI_CR2_TXDMAEN;
  TIM1->CNT = 2;
  /* Enable Tx DMA Request */
  SPI1->CR2 = cr2;

  int i = 0;
  while (1) {
    HAL_Delay(20);
    cap_sense();
  }
}

void NMI_Handler() { while (1) { } }
void HardFault_Handler() { while (1) { } }
void SVC_Handler() { while (1) { } }
void PendSV_Handler() { while (1) { } }
void SysTick_Handler()
{
  HAL_IncTick();
}
void DMA1_Channel1_IRQHandler()
{
  HAL_DMA_IRQHandler(&dma1_ch1);
}

void SPI1_IRQHandler()
{
  HAL_SPI_IRQHandler(&spi1);
}
static inline void refill_buffer(uint16_t *buf)
{
/*
  if (touch_active)
    for (int i = 0; i < AUDIO_BUF_HALF_SIZE; i++) buf[i] = sine_table[i];
  else
    for (int i = 0; i < AUDIO_BUF_HALF_SIZE; i++) buf[i] = 0;
*/
  synth_audio((int16_t *)buf, AUDIO_BUF_HALF_SIZE);
}
void HAL_SPI_TxHalfCpltCallback(SPI_HandleTypeDef *_spi)
{
  refill_buffer(audio_buf);
}
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *_spi)
{
  refill_buffer(audio_buf + AUDIO_BUF_HALF_SIZE);
}

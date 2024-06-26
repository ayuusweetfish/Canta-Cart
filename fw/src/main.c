#include "py32f0xx_hal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SYNTH_SAMPLE_RATE 31250
#define SYNTH_EXTRA_SHIFT (-2)
#define SYNTH_STEREO
#include "../../misc/synth/canta_synth.h"

#define min(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define max(_a, _b) ((_a) > (_b) ? (_a) : (_b))

#define RELEASE

// #define R_470K
#ifdef R_470K
#define TOUCH_ON_THR  180
#define TOUCH_OFF_THR  80
#else
#define TOUCH_ON_THR  500
#define TOUCH_OFF_THR 200
#endif

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

void dma_tx_half_cplt();
void dma_tx_cplt();

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
    last_v = ~0x75fd;
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
    last_v = 0x75fd;
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
  static bool btns[12] = { false };
  for (int j = 0; j < 12; j++)
    if (!btns[j] && cap_sum[j] > TOUCH_ON_THR) btns[j] = true;
    else if (btns[j] && cap_sum[j] < TOUCH_OFF_THR) btns[j] = false;
#ifndef RELEASE
  btns[9] = btns[11] = false;
#endif
  __disable_irq();
  synth_buttons(btns);
  __enable_irq();
  // for (int j = 0; j < 12; j++) swv_printf("%d%c", (int)btns[j], j == 11 ? '\n' : ' ');
}

uint16_t audio_buf[256] = { 0 };
#define AUDIO_BUF_HALF_SIZE ((sizeof audio_buf / sizeof audio_buf[0]) / 2)

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
                  GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_12
#ifdef RELEASE
                | GPIO_PIN_13 | GPIO_PIN_14
#endif
                ;
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
  __HAL_TIM_MOE_ENABLE(&tim1);
  // Enable timer PWM output
  TIM1->CCER |= (TIM_CCx_ENABLE << TIM_CHANNEL_3);

  // Enable SPI and DMA channel
  dma1_ch1.XferHalfCpltCallback = dma_tx_half_cplt;
  dma1_ch1.XferCpltCallback = dma_tx_cplt;
  dma1_ch1.XferErrorCallback = NULL;
  dma1_ch1.XferAbortCallback = NULL;

  // NOTE: This is number of transfers that gets written to DMA_CNDTRx,
  // but reference manual says CNDTR (NDT) is number of bytes?
  // Is datasheet copied from STM32F103 but PY32 actually follows STM32G0x0?
  HAL_DMA_Start_IT(&dma1_ch1, (uint32_t)audio_buf, (uint32_t)&SPI1->DR,
    sizeof audio_buf / sizeof audio_buf[0]);

  // Enable SPI DMA transmit request
  uint32_t spi1_cr2 = SPI1->CR2 | SPI_CR2_TXDMAEN;
  SPI1->CR2 = spi1_cr2;

  // Enable SPI
  uint32_t spi1_cr1 = SPI1->CR1 | SPI_CR1_SPE;
  // Enable timer
  uint32_t tim1_cr1 = TIM1->CR1 | TIM_CR1_CEN;

  uint32_t addr_scratch;
  __asm__ volatile (
    "ldr %0, =%3\n"
    "str %4, [%0, #0]\n"  // TIM1->CR1 = tim1_cr1;
    "ldr %0, =%1\n"
    "str %2, [%0, #0]\n"  // TIM1->CNT = 0;
    "nop\n" // 30 cycles, as timer prescaler is 32
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "ldr %0, =%5\n"
    "str %6, [%0, #0]\n"  // SPI1->CR1 = spi1_cr1;
    : "=&l" (addr_scratch)
    : "i" (&TIM1->CNT), "l" (1),
      "i" (&TIM1->CR1), "l" (tim1_cr1),
      "i" (&SPI1->CR1), "l" (spi1_cr1)
    : "memory"
  );

  while (1) {
    HAL_Delay(1);
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

static inline void refill_buffer(uint16_t *buf)
{
  // for (int i = 0; i < AUDIO_BUF_HALF_SIZE; i++)
  //   buf[i] = (last_btn[0] && i % 128 < 64) ? 0x01 : 0;
  synth_audio((int16_t *)(buf + 1), AUDIO_BUF_HALF_SIZE - 1);
}
void dma_tx_half_cplt()
{
  refill_buffer(audio_buf);
}
void dma_tx_cplt()
{
  refill_buffer(audio_buf + AUDIO_BUF_HALF_SIZE);
}

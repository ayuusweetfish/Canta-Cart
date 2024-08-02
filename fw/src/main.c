#include "py32f0xx_hal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SYNTH_SAMPLE_RATE 31250
#define SYNTH_EXTRA_SHIFT (-1)
#define SYNTH_STEREO
#include "../../misc/synth/canta_synth.h"

#define min(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define max(_a, _b) ((_a) > (_b) ? (_a) : (_b))

// To re-flash after a release build, pull PF4 (BTN_OUT) high before power-on
#define RELEASE
// #define PD_BTN_1     // Pull down button 1 to provide a ground probe clip
// #define INSPECT_ONLY // Output sensed values to debugger, disable sound output
// #define INSPECT      // Output sensed values to debugger

#define TOUCH_HARD_ON_THR 120
#define TOUCH_SOFT_ON_THR  50
#define TOUCH_OFF_THR      30
/*
Values for reference without conformal coating:
#define TOUCH_HARD_ON_THR 400
#define TOUCH_SOFT_ON_THR 150
#define TOUCH_OFF_THR     100
*/

#define BTN_OUT_PORT GPIOF
#define BTN_OUT_PIN  GPIO_PIN_4

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
TIM_HandleTypeDef tim1, tim17;

void dma_tx_half_cplt();
void dma_tx_cplt();

// Pull a set of pins to a given level and set them as input
static inline void pull_electrodes_port(GPIO_TypeDef *port, uint32_t pins, bool level)
{
  GPIO_InitTypeDef gpio_init = (GPIO_InitTypeDef){
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pin = pins,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
  };
  HAL_GPIO_Init(port, &gpio_init);
  HAL_GPIO_WritePin(port, pins, level);
  gpio_init.Mode = GPIO_MODE_INPUT;
  HAL_GPIO_Init(port, &gpio_init);
}

static inline void pull_electrodes(bool level)
{
  pull_electrodes_port(GPIOA,
  #ifndef PD_BTN_1
    GPIO_PIN_0 |
  #endif
    GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
    GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_12 |
  #ifdef RELEASE
    GPIO_PIN_13 | GPIO_PIN_14 |
  #endif
    0, level);

  pull_electrodes_port(GPIOF, GPIO_PIN_2, level);

#ifdef PD_BTN_1
  GPIO_InitTypeDef gpio_init = (GPIO_InitTypeDef){
    .Pin = GPIO_PIN_0,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_PULLDOWN,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 0);
#endif
}

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
    1 <<  0,
    1 <<  1,
    1 <<  2,
    1 <<  3,
    1 <<  4,
    1 <<  5,
    1 <<  6,
    1 <<  7,
    1 << 12,
    1 << 13,
    1 <<  8,
    1 << 14,
  };
  static const uint16_t FULL_MASK =
    MASK[ 0] | MASK[ 1] | MASK[ 2] | MASK[ 3] | MASK[ 4] | MASK[ 5] |
    MASK[ 6] | MASK[ 7] | MASK[ 8] | MASK[ 9] | MASK[10] | MASK[11];

  inline void toggle(const bool level) {
    pull_electrodes(1 - level); // Pull to the opposite level before reading
    int n_records = 0;
    uint16_t last_v = (level == 1 ? ~FULL_MASK : FULL_MASK);
    record[n_records] = (struct record_t){.t = (uint16_t)-1, .v = last_v};
    __disable_irq();
    HAL_GPIO_WritePin(BTN_OUT_PORT, BTN_OUT_PIN, level);
    for (int i = 0; i < 200; i++) {
      uint32_t a = GPIOA->IDR;
      uint32_t f = GPIOF->IDR;
      uint16_t combined_v = a | (f << 6);
      uint16_t cur_v = (level == 1 ? (combined_v | last_v) : (combined_v & last_v));
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
  }
  for (int its = 0; its < 5; its++) {
    toggle(1);
    toggle(0);
  }

  // Maintain a base value for each of the buttons

  // The base value increases by 1 every 1000 iterations (a few seconds)
  static const uint32_t BASE_MULT = 1024;
  static uint32_t base[12] = { UINT32_MAX };
  if (base[0] == UINT32_MAX) {
    for (int j = 0; j < 12; j++) base[j] = cap_sum[j] * BASE_MULT;
  } else {
    for (int j = 0; j < 12; j++) {
      base[j] += 1;
      if (base[j] > cap_sum[j] * BASE_MULT)
        base[j] = cap_sum[j] * BASE_MULT;
    }
  }
  for (int j = 0; j < 12; j++) cap_sum[j] -= base[j] / BASE_MULT;

  // A button is considered turned-on, if its sensed value exceeds `TOUCH_HARD_ON_THR`
  // or exceeds the larger of the nearby buttons' by `TOUCH_SOFT_ON_THR`

  static bool btns[12] = { false };
  for (int j = 0; j < 12; j++) {
    if (!btns[j]) {
      if (cap_sum[j] > TOUCH_HARD_ON_THR) btns[j] = true;
      else if (j < 10 && cap_sum[j] > TOUCH_SOFT_ON_THR) {
        uint16_t nearby = 0;
        if (j > 0) nearby = cap_sum[j - 1];
        if (j < 9 && nearby < cap_sum[j + 1]) nearby = cap_sum[j + 1];
        if (cap_sum[j] > nearby + TOUCH_SOFT_ON_THR)
          btns[j] = true;
      }
    } else {
      if (cap_sum[j] < TOUCH_OFF_THR) btns[j] = false;
    }
  }

#ifndef RELEASE
  btns[9] = btns[11] = false;
#endif
#ifdef PD_BTN_1
  for (int i = 0; i < 12; i++) if (i != 7) btns[i] = false;
#endif
#if defined(INSPECT_ONLY) || defined(INSPECT)
  for (int j = 0; j < 12; j++) swv_printf("%3d%c", min(999, cap_sum[j]), j == 11 ? '\n' : ' ');
  #if defined(INSPECT_ONLY)
  for (int j = 0; j < 12; j++) btns[j] = false;
  #endif
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
  // FLASH->OPTR |= FLASH_OPTR_NRST_MODE;  // Cannot be modified after POR
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

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  GPIO_InitTypeDef gpio_init;

  // ======== Capacitive touch sensing ========
  // BTN_OUT
  gpio_init = (GPIO_InitTypeDef){
    .Pin = BTN_OUT_PIN,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
  };
  HAL_GPIO_Init(BTN_OUT_PORT, &gpio_init);
  HAL_GPIO_WritePin(BTN_OUT_PORT, BTN_OUT_PIN, 0);

  // For the electrodes, refer to `pull_electrodes()`

  // ======== SPI ========
  // PB5 AF0 SPI1_MOSI
  gpio_init = (GPIO_InitTypeDef){
    .Mode = GPIO_MODE_AF_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
    .Alternate = GPIO_AF0_SPI1,
  };
  // gpio_init.Pin = GPIO_PIN_1; HAL_GPIO_Init(GPIOA, &gpio_init);
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
  // PB6 AF1 TIM1_CH3
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
  TIM1->CCER |= TIM_CCER_CC3E;

  // PB7 AF2 TIM17_CH1N
  gpio_init = (GPIO_InitTypeDef){
    .Pin = GPIO_PIN_7,
    .Mode = GPIO_MODE_AF_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
    .Alternate = GPIO_AF2_TIM17,
  };
  HAL_GPIO_Init(GPIOB, &gpio_init);

  __HAL_RCC_TIM17_CLK_ENABLE();
  tim17 = (TIM_HandleTypeDef){
    .Instance = TIM17,
    .Init = {
      .Prescaler = 0,
      .CounterMode = TIM_COUNTERMODE_DOWN,
      .Period = 31,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
    },
  };
  HAL_TIM_PWM_Init(&tim17);
  HAL_TIM_PWM_ConfigChannel(&tim17, &(TIM_OC_InitTypeDef){
    .OCMode = TIM_OCMODE_PWM1,
    .Pulse = 15,
    .OCPolarity = TIM_OCPOLARITY_HIGH,
    .OCNPolarity = TIM_OCNPOLARITY_HIGH,
    .OCFastMode = TIM_OCFAST_DISABLE,
    .OCIdleState = TIM_OCIDLESTATE_RESET,
    .OCNIdleState = TIM_OCNIDLESTATE_RESET,
  }, TIM_CHANNEL_1);
  __HAL_TIM_MOE_ENABLE(&tim17);
  // Enable timer PWM output
  TIM17->CCER |= TIM_CCER_CC1NE;

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
  SPI1->CR2 |= SPI_CR2_TXDMAEN;

  // Separate into a function so that distance to the literal pool does not get too large
  __attribute__((noinline))
  void synchronised_start() {
    uint32_t addr_scratch, val_scratch, opnd_scratch;
    __asm__ volatile (
      // TIM1->CR1 |= TIM_CR1_CEN;
      "ldr %[addr], =%[tim1_cr1]\n"
      "ldr %[val], [%[addr], #0]\n"
      "ldr %[opnd], =%[tim_cr1_cen]\n"
      "orr %[val], %[opnd]\n"
      "str %[val], [%[addr], #0]\n"
      // TIM1->CNT = <tim1_cnt_val>;
      "ldr %[addr], =%[tim1_cnt]\n"
      "ldr %[val], =%[tim1_cnt_val]\n"
      "str %[val], [%[addr], #0]\n"
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
      // TIM17->CR1 |= TIM_CR1_CEN;
      "ldr %[addr], =%[tim17_cr1]\n"
      "ldr %[val], [%[addr], #0]\n"
      "ldr %[opnd], =%[tim_cr1_cen]\n"
      "orr %[val], %[opnd]\n"
      "str %[val], [%[addr], #0]\n"
      // TIM17->CNT = <tim17_cnt_val>;
      "ldr %[addr], =%[tim17_cnt]\n"
      "ldr %[val], =%[tim17_cnt_val]\n"
      "str %[val], [%[addr], #0]\n"
      // SPI1->CR1 |= SPI_CR1_SPE;
      "ldr %[addr], =%[spi1_cr1]\n"
      "ldr %[val], [%[addr], #0]\n"
      "ldr %[opnd], =%[spi_cr1_spe]\n"
      "orr %[val], %[opnd]\n"
      "str %[val], [%[addr], #0]\n"
      : [addr] "=&l" (addr_scratch),
        [val] "=&l" (val_scratch),
        [opnd] "=&l" (opnd_scratch)
      : [tim1_cnt] "i" (&TIM1->CNT), [tim1_cnt_val] "i" (2),
        [tim1_cr1] "i" (&TIM1->CR1), [tim_cr1_cen] "i" (TIM_CR1_CEN),
        [tim17_cnt] "i" (&TIM17->CNT), [tim17_cnt_val] "i" (27),
        [tim17_cr1] "i" (&TIM17->CR1),
        [spi1_cr1] "i" (&SPI1->CR1), [spi_cr1_spe] "i" (SPI_CR1_SPE)
      : "memory"
    );
  }
  synchronised_start();

  uint32_t last_tick = HAL_GetTick();
  while (1) {
    uint32_t cur_tick;
    while ((cur_tick = HAL_GetTick()) - last_tick < 1) { }
    last_tick = cur_tick;
    cap_sense();
    // swv_printf("%lu\n", HAL_GetTick() - cur_tick);
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
  // for (int i = 0; i < AUDIO_BUF_HALF_SIZE; i++) buf[i] = 0x1;
}
void dma_tx_half_cplt()
{
  refill_buffer(audio_buf);
}
void dma_tx_cplt()
{
  refill_buffer(audio_buf + AUDIO_BUF_HALF_SIZE);
}

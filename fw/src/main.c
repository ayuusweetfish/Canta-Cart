#include "py32f0xx_hal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 1);
    for (int i = 0; i < 200; i++) {
      uint32_t a = GPIOA->IDR;
      uint32_t f = GPIOF->IDR;
      uint16_t cur_v = (a | (f << 6)) | last_v;
      if (last_v != cur_v) n_records++;
      record[n_records] = (struct record_t){.t = i, .v = cur_v};
      last_v = cur_v;
    }
  /*
    for (int i = 0; i <= n_records; i++)
      swv_printf("%3d %08x\n", (int)(int16_t)record[i].t, record[i].v);
  */
    for (int j = 0; j < 12; j++) cap[j] = 0xffff;
    for (int i = 1; i <= n_records; i++) {
      uint16_t t = record[i - 1].t + 1;
      uint16_t diff = record[i - 1].v ^ record[i].v;
      // swv_printf("%3d %04x\n", t, diff);
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
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 0);
    for (int i = 0; i < 200; i++) {
      uint32_t a = GPIOA->IDR;
      uint32_t f = GPIOF->IDR;
      uint16_t cur_v = (a | (f << 6)) & last_v;
      if (last_v != cur_v) n_records++;
      record[n_records] = (struct record_t){.t = i, .v = cur_v};
      last_v = cur_v;
    }
    for (int j = 0; j < 12; j++) cap[j] = 0xffff;
    for (int i = 1; i <= n_records; i++) {
      uint16_t t = record[i - 1].t + 1;
      uint16_t diff = record[i - 1].v ^ record[i].v;
      // swv_printf("%3d %04x\n", t, diff);
      for (int j = 0; j < 12; j++)
        if (diff & MASK[j]) cap[j] = t;
    }
    for (int j = 0; j < 12; j++)
      if (cap_sum[j] == 0xffff || cap[j] == 0xffff) cap_sum[j] = 0xffff;
      else cap_sum[j] += cap[j];
    for (volatile int j = 0; j < 2000; j++) { }
  }

  for (int j = 0; j < 12; j++) swv_printf("%3d%c", min(999, cap_sum[j]), j == 11 ? '\n' : ' ');
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

  // Option byte
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
  // FLASH->OPTR |= FLASH_OPTR_NRST_MODE;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  GPIO_InitTypeDef gpio_init;

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

  int i = 0;
  while (1) {
    HAL_Delay(100);
    // swv_printf("Hello! %d\n", ++i);
    // swv_printf("sys clock = %u\n", HAL_RCC_GetSysClockFreq());
    // swv_printf("FLASH_OPTR = %08x FLASH_OPTR_NRST_MODE = %08x\n", FLASH->OPTR, FLASH_OPTR_NRST_MODE);
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

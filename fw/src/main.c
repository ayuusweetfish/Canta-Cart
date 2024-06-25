#include "py32f0xx_hal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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
/*
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 1);
  uint32_t a[100];
  for (int i = 0; i < 100; i++) {
    a[i] = GPIOA->IDR & 0x10fd;
  }
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 0);
  for (int i = 0; i < 100; i++)
    if (i == 0 || i + 1 == 100 || a[i] != a[i - 1])
      swv_printf("%3d %08lx\n", i, a[i]);
*/

  struct record_t {
    uint16_t t;
    uint16_t v;
  } record[16];
  int n_records = 0;

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 1);

  uint16_t last_v = 0;
  for (int i = 0; i < 200; i++) {
    uint32_t a = GPIOA->IDR;
    uint32_t f = GPIOF->IDR;
    uint16_t cur_v = (a & 0x10fd) | (f << 6) | last_v;
    record[n_records] = (struct record_t){.t = i, .v = cur_v};
    if (last_v != cur_v) n_records++;
    last_v = cur_v;
  }
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 0);

  for (int i = 0; i < n_records; i++)
    swv_printf("%3d %08x\n", record[i].t, record[i].v);
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
    HAL_Delay(1000);
    swv_printf("Hello! %d\n", ++i);
    swv_printf("sys clock = %u\n", HAL_RCC_GetSysClockFreq());
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

#include "CH59x_common.h"
#include "printf.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

void _putchar(char character)
{
  if (character == '\n') {
    while (R8_UART1_TFC == UART_FIFO_SIZE) { }
    R8_UART1_THR = '\r';
  }
  while (R8_UART1_TFC == UART_FIFO_SIZE) { }
  R8_UART1_THR = character;
}

// ============ Touch ============ //

#define TOUCH_HARD_ON_THR 120
#define TOUCH_SOFT_ON_THR  60
#define TOUCH_OFF_THR      40

enum gpio_port {
  GPIOA = 0,
  GPIOB = 1,
};

// Pull a set of pins to a given level and set them as input
static inline void pull_electrodes_port(int port, uint32_t pins, bool level)
{
}

static inline void pull_electrodes(bool level)
{
  pull_electrodes_port(
    GPIOA, (1 << 4) | (1 << 5) | (0 << 8) | (0 << 9) | (1 << 15),
    level);
  pull_electrodes_port(
    GPIOB, (1 << 4) | (1 << 7) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 23),
    level);
}

static inline void pull_out(bool level)
{
  if (level) GPIOA_SetBits(1 << 10);
  else       GPIOA_ResetBits(1 << 10);
}

static uint32_t saved_irq;
static inline void disable_irq() { SYS_DisableAllIrq(&saved_irq); }
static inline void enable_irq() { SYS_RecoverIrq(saved_irq); }

#pragma GCC optimize("O3")
static inline void cap_sense()
{
  struct record_t {
    uint32_t t;
    uint32_t v;
  } record[16];

  uint16_t cap[12] = { 0 };
  uint16_t cap_sum[12] = { 0 };

  // read vector = (A << 1) | B
  static const uint32_t MASK[12] = {
    1 << 10,  // PA9
    1 << 15,
    1 << 14,
    1 << 13,
    1 << 12,
    1 <<  7,
    1 <<  4,
    1 << 23,
    1 <<  5,  // PA4
    1 <<  6,  // PA5
    1 <<  9,  // PA8
    1 << 16,  // PA15
  };
  static const uint32_t FULL_MASK =
    MASK[ 0] | MASK[ 1] | MASK[ 2] | MASK[ 3] | MASK[ 4] | MASK[ 5] |
    MASK[ 6] | MASK[ 7] | MASK[ 8] | MASK[ 9] | MASK[10] | MASK[11];

  inline void toggle(const bool level) {
    pull_electrodes(1 - level); // Pull to the opposite level before reading
    int n_records = 0;
    uint32_t last_v = (level == 1 ? ~FULL_MASK : FULL_MASK);
    record[n_records] = (struct record_t){.t = (uint16_t)-1, .v = last_v};
    disable_irq();
    pull_out(level);
    for (int i = 0; i < 100; i++) {
      uint32_t a = (R32_PA_PIN);
      uint32_t b = (R32_PB_PIN);
      const uint32_t a_mask = (1 << 4) | (1 << 5) | (0 << 8) | (0 << 9) | (1 << 15);
      uint32_t combined_v = ((a << 1) & (a_mask << 1)) | (b & ~(a_mask << 1));
      uint32_t cur_v = (level == 1 ? (combined_v | last_v) : (combined_v & last_v));
      if (last_v != cur_v) n_records++;
      record[n_records] = (struct record_t){.t = i, .v = cur_v};
      last_v = cur_v;
    }
    enable_irq();
    for (int j = 0; j < 12; j++) cap[j] = 0xffff;
    for (int i = 1; i <= n_records; i++) {
      uint16_t t = record[i - 1].t + 1;
      uint32_t diff = record[i - 1].v ^ record[i].v;
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

  for (int j = 0; j < 12; j++) printf("%3d%c%c", min(999, cap_sum[j]), btns[j] ? '*' : '.', j == 11 ? '\n' : ' ');
}

// ============ Audio ============ //

#define AUDIO_BUF_N_BYTES 4000
static uint8_t audio_buf[AUDIO_BUF_N_BYTES];

static void refill_audio(int half)
{
  uint8_t *a = audio_buf + half * (AUDIO_BUF_N_BYTES / 2);

  static int phase = 0;
  static const int16_t sine_table[] = {
/*
import math
print(','.join('%d' % (4000 * math.sin(i/66. * math.pi * 2)) for i in range(66)))
*/
    0,380,757,1126,1486,1832,2162,2472,2760,3022,3258,3464,3638,3780,3887,3959,3995,3995,3959,3887,3780,3638,3464,3258,3022,2760,2472,2162,1832,1486,1126,757,380,0,-380,-757,-1126,-1486,-1832,-2162,-2472,-2760,-3022,-3258,-3464,-3638,-3780,-3887,-3959,-3995,-3995,-3959,-3887,-3780,-3638,-3464,-3258,-3022,-2760,-2472,-2162,-1832,-1486,-1126,-757,-380
  };
  for (int i = 0; i < (AUDIO_BUF_N_BYTES / 8); i++) {
    int16_t sample = sine_table[phase] * 4;
    // int16_t sample = 0x0105;
    if ((phase += 3) >= sizeof sine_table / sizeof sine_table[0]) phase = 0;
    a[i * 4 + 0] = a[i * 4 + 2] = (uint8_t)((sample >> 8) & 0xff);
    a[i * 4 + 1] = a[i * 4 + 3] = (uint8_t)((sample >> 0) & 0xff);
  }

  static int count = 0;
  if (++count == 29296 / (AUDIO_BUF_N_BYTES / 2 / 2)) {
    printf("!\n");
    count = 0;
  }
}

// interrupt(WCH-Interrupt-fast") in standard toolchain
// https://github.com/riscv/riscv-fast-interrupt/issues/329
__attribute__((section(".highcode")))
__attribute__((naked))
void TMR1_IRQHandler_(void)
{
  asm volatile ("call TMR1_IRQHandler_actual; mret");
}

__attribute__((section(".highcode")))
void TMR1_IRQHandler_actual()
{
  TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
  uint16_t dma_start_addr = (uint16_t)(uintptr_t)audio_buf & ((1 << 14) - 1);
  uint16_t dma_pos = R16_SPI0_DMA_NOW - dma_start_addr;

  // Refill?
  static int last_half = 0;
  int cur_half = (dma_pos >= AUDIO_BUF_N_BYTES / 2);
  if (last_half != cur_half) {
    refill_audio(last_half);
    last_half = cur_half;
  }

  R16_SPI0_TOTAL_CNT = 4095;  // Keep running
}

__attribute__((section(".highcode")))
void TMR1_IRQHandler() {
  TMR1_IRQHandler_actual();
}

static void i2s_init()
{
  // ============ Audio ============ //

if (1) {
  // WS: PA11 TMR2
  GPIOA_ResetBits(GPIO_Pin_11);
  GPIOA_ModeCfg(GPIO_Pin_11, GPIO_ModeOut_PP_5mA);
  GPIOPinRemap(DISABLE, RB_PIN_TMR2);
} else {
  // WS: PB23 TMR0_
  GPIOB_ResetBits(GPIO_Pin_23);
  GPIOB_ModeCfg(GPIO_Pin_23, GPIO_ModeOut_PP_5mA);
  GPIOPinRemap(ENABLE, RB_PIN_TMR0);
}
  R8_TMR2_CTRL_MOD = RB_TMR_OUT_EN | (High_Level << 4) | (PWM_Times_1 << 6);
  // Fsys = 60 MHz, prescale = 64, bit depth = 16 (2 channels)
  // -> Fsample = 29.296 kHz
  int half_period = 64 * 16 + 4;
    // SCK appears actually 4 SysClock cycles slower, accomodate
  TMR2_PWMCycleCfg(half_period * 2);
  TMR2_PWMActDataWidth(half_period);

  // Data output by SPI
if (1) {
  // BCK: PA13 SCK0
  // DATA: PA14 MOSI
  GPIOA_ResetBits(GPIO_Pin_13 | GPIO_Pin_14);
  GPIOA_ModeCfg(GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
  GPIOPinRemap(DISABLE, RB_PIN_SPI0);
} else {
  GPIOB_ResetBits(GPIO_Pin_13 | GPIO_Pin_14);
  GPIOB_ModeCfg(GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
  GPIOPinRemap(ENABLE, RB_PIN_SPI0);
}
  SPI0_MasterDefInit();
  SPI0_CLKCfg(64);
  R8_SPI0_CTRL_CFG |= RB_SPI_DMA_LOOP;

  R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR;
  R16_SPI0_DMA_BEG = (uint32_t)audio_buf;
  R16_SPI0_DMA_END = (uint32_t)(audio_buf + AUDIO_BUF_N_BYTES);
  R16_SPI0_TOTAL_CNT = 4095;  // Lowest 12 bits effective

__attribute__((noinline))
void synchronized_enable()
{
  __asm__ volatile ("" ::: "memory");
  R8_TMR2_CTRL_MOD |= RB_TMR_COUNT_EN;
  __asm__ volatile ("" ::: "memory");
  __asm__ volatile (
    "nop\n nop\n nop\n nop\n"  "nop\n nop\n nop\n nop\n"
    "nop\n nop\n nop\n nop\n"
  );
  __asm__ volatile ("" ::: "memory");
  R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE;
  __asm__ volatile ("" ::: "memory");
}

  synchronized_enable();

  TMR1_TimerInit(64 * 16 * 1000);
  TMR1_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
  PFIC_EnableIRQ(TMR1_IRQn);
  SetVTFIRQ((uint32_t)(uintptr_t)&TMR1_IRQHandler_, TMR1_IRQn, 0, ENABLE);
}

int main()
{
  PWR_DCDCCfg(DISABLE);
  SetSysClock(CLK_SOURCE_PLL_60MHz);
  SysTick_Config((1ULL << 32) - 1);
  PFIC_DisableIRQ(SysTick_IRQn);

  GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_Floating);
  GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_Floating);

  // Debug usage. Monitor serial output at PA9 = TXD1 = bottom-left touch pad
  GPIOA_SetBits(GPIO_Pin_9);
  // GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU); // Probably no need
  GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
  UART1_DefInit();
  printf("CHIP_ID = %02x\n", R8_CHIP_ID);

  // ACT LED
  GPIOB_SetBits(GPIO_Pin_22);
  GPIOB_ModeCfg(GPIO_Pin_22, GPIO_ModeOut_PP_5mA);

  i2s_init();

  int i = 0;

if (0) {
  // CAP_OUT
  GPIOA_ModeCfg(GPIO_Pin_10, GPIO_ModeOut_PP_5mA);

  while (1) {
    cap_sense();
    DelayMs(50);
    if (++i == 10) { R32_PB_OUT ^= (1 << 22); i = 0; }
  }
}

  while (1) {
    if (i != 0) DelayMs(500); GPIOB_SetBits(GPIO_Pin_22);
    GPIOB_SetBits(GPIO_Pin_23);
    DelayMs(500); GPIOB_ResetBits(GPIO_Pin_22);
    GPIOB_ResetBits(GPIO_Pin_23);
    printf("tick\n");
    i = 1;
  }
}

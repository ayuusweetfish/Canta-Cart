#include "CH59x_common.h"
#include "printf.h"
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

  int i = 0;
  while (1) {
    if (i != 0) DelayMs(500); GPIOB_SetBits(GPIO_Pin_22);
    DelayMs(500); GPIOB_ResetBits(GPIO_Pin_22);
    printf("tick\n");
    i = 1;
  }
}

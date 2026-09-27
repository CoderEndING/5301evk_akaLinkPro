/*
 * STM32H743 · SEGGER RTT **吞吐测试固件**（与 stm32f103_rtt_speed 同一套量法）
 *
 *   while(1) 里死循环发 "hello world!\n"，不加任何延时。
 *   RTT 用 BLOCK_IF_FIFO_FULL：缓冲满就阻塞 —— 目标写多快完全由主机取多快决定，
 *   主机读到的字节/秒就是 RTT 的实际吞吐。
 *
 * H743 与 F103 版的差别：
 *   1. RTT 控制块/环形缓冲必须在 **AXI SRAM(0x24000000)**（见 ld 脚本的说明：
 *      DTCM 走 AHB-AP 读不到，探针够不着）；
 *   2. 复位默认就跑 HSI 64 MHz，**不配 PLL 也能测**；要飙高频时由上位机在
 *      halted 状态下改 RCC（与 F103 版同样的做法），固件不必改；
 *   3. 关着 D-Cache（DCache 会让探针读到过期的环形缓冲），I-Cache 开着帮循环取指。
 */
#include <stdint.h>

#include "SEGGER_RTT.h"

#define FLASH_ACR (*(volatile uint32_t *)0x52002000) /* H7: FLASH 在 0x52002000 */
#define SCB_CCR   (*(volatile uint32_t *)0xE000ED14)
#define SCB_ICIALLU (*(volatile uint32_t *)0xE000EF50)
#define SYST_CSR  (*(volatile uint32_t *)0xE000E010)
#define SYST_RVR  (*(volatile uint32_t *)0xE000E014)
#define SYST_CVR  (*(volatile uint32_t *)0xE000E018)

volatile uint32_t g_bytes;
volatile uint32_t g_loops;
volatile uint32_t g_ms;

void SysTick_Handler(void){ g_ms++; }

int main(void){
  /* 复位默认 VOS3 + HSI 64 MHz：先给 flash 足等待周期再把 I-Cache 打开，
   * D-Cache 故意不开（缓存会让探针读到的 RTT 缓冲不新鲜）。 */
  FLASH_ACR = (FLASH_ACR & ~0xFu) | 2u;         /* latency = 2 WS */
  SCB_ICIALLU = 0;
  SCB_CCR |= (1u << 17);                        /* ICACHE */

  SEGGER_RTT_Init();
  SEGGER_RTT_WriteString(0, "\r\n=== H743 RTT 吞吐测试：BLOCK_IF_FIFO_FULL，死循环发 hello world ===\r\n");

  SYST_RVR = 64000u - 1u;                       /* HSI 64 MHz -> 1 ms */
  SYST_CVR = 0;
  SYST_CSR = 7;

  static const char msg[] = "hello world!\n";   /* 13 字节 */
  for (;;){
    unsigned n = SEGGER_RTT_Write(0, msg, sizeof(msg) - 1);
    g_bytes += n;
    g_loops++;
  }
}

/*
 * HPM6800EVK (HPM6880, RISC-V) · J-Scope 靶子固件
 *
 * 与 script_test/stm32f103_scope 同一套路：一份**已知契约**的变量块 + 已知时基，
 * 让探针的 HSS 采样器（HID 0x32 + bulk IN 0x83，走 JTAG 的 SBA 后端）采回来之后
 * 能逐项核对"采到的到底对不对"，而不只是"有多少包"。
 *
 * 契约（8 × u32 = 32 B，全部 4 字节对齐，`g_v` 的地址构建后用 nm 查）：
 *
 *   | 偏移 | 名字    | 类型 | 内容 |
 *   | --- | --- | --- | --- |
 *   | +0  | g_tick  | u32  | 10 kHz 计数（每 100 µs +1）—— 用来验时基/斜率 |
 *   | +4  | u_hi    | u32  | 高位恒为 1（0x1xxxxxxx）—— 高位校验用 |
 *   | +8  | f_sin   | f32  | 500 Hz 正弦，-1..+1 |
 *   | +12 | f_tri   | f32  | 500 Hz 三角，-1..+1 |
 *   | +16 | i_sq1k  | i32  | 1 kHz 方波 ±1000 |
 *   | +20 | i_sq5k  | i32  | 5 kHz 方波 ±1000（采样率 < 10 kHz 必混叠，专门留的坑） |
 *   | +24 | u_ramp  | u32  | 0..999 每拍 +1 的斜坡（10 kHz 周期 = 100 ms） |
 *   | +28 | lfsr    | u32  | 32 位 LFSR，每拍一步（伪随机，验"值在动"） |
 *
 * 时基：MCHTMR（machine timer）直接轮询，每 `f/10000` 个计数更新一拍。不用中断
 * —— 靶子的职责只是"让变量按已知速率变化"，polling 的抖动是纳秒级，比 ISR 简单
 * 且不依赖 SDK 的定时器驱动。实测频率上报在 `g_mchtmr_hz`，主机可以据此核对
 * "10 kHz 时基"是不是真的 10 kHz。
 *
 * 构建/烧录：见同目录 README。变量地址：
 *   riscv32-unknown-elf-nm -S build/flash_xip/output/hpm6800evk_scope.elf | findstr g_v
 */
#include <stdint.h>

#include "board.h"
#include "hpm_clock_drv.h"
#include "hpm_l1c_drv.h"
#include "hpm_mchtmr_drv.h"

typedef struct
{
    volatile uint32_t tick;
    volatile uint32_t u_hi;
    volatile float    f_sin;
    volatile float    f_tri;
    volatile int32_t  i_sq1k;
    volatile int32_t  i_sq5k;
    volatile uint32_t u_ramp;
    volatile uint32_t lfsr;
} scope_vars_t;

volatile scope_vars_t g_v;          /* ← 主机要的地址就是这个符号（32 B，一个 span） */
volatile uint32_t g_mchtmr_hz;      /* 实测 MCHTMR 频率（给主机对账时基） */
volatile uint32_t g_updates;        /* 更新次数（= tick 的镜像，便于"到底在不在跑"） */

/* 500 Hz 正弦：20 个点一个周期（10 kHz / 20 = 500 Hz）。
 * 用查表而不是 sinf()：一是避免拖 libm 进固件，二是表就是契约的一部分。 */
static const float kSin20[20] = {
     0.000000f,  0.309017f,  0.587785f,  0.809017f,  0.951057f,
     1.000000f,  0.951057f,  0.809017f,  0.587785f,  0.309017f,
     0.000000f, -0.309017f, -0.587785f, -0.809017f, -0.951057f,
    -1.000000f, -0.951057f, -0.809017f, -0.587785f, -0.309017f,
};

int main(void)
{
    board_init();
    board_init_led_pins();

    uint32_t hz = (uint32_t)clock_get_frequency(clock_mchtmr0);
    g_mchtmr_hz = hz;
    if (hz < 100000U)
    {
        hz = 24000000U;                 /* 时钟没起来时的兜底：按 24 MHz 算，契约照旧 */
    }

    const uint32_t step = hz / 10000U;  /* 100 µs 一拍 */
    uint32_t next = (uint32_t)mchtmr_get_count(HPM_MCHTMR) + step;
    uint32_t t = 0U;
    uint32_t lfsr = 0x12345678U;

    for (;;)
    {
        uint32_t now = (uint32_t)mchtmr_get_count(HPM_MCHTMR);

        /* 只推进到"到点了"为止：轮询抖动是几十 ns，10 kHz 完全够用 */
        if ((int32_t)(now - next) < 0)
        {
            continue;
        }
        next += step;
        t++;

        uint32_t p10 = t % 10U;                       /* 1 kHz 相位（10 拍一周期） */
        uint32_t p20 = t % 20U;                       /* 500 Hz 相位 */

        g_v.tick   = t;
        g_v.u_hi   = 0x10000000U | (t & 0xFFFFU);
        g_v.f_sin  = kSin20[p20];
        g_v.f_tri  = ((float)((p20 < 10U) ? p20 : (20U - p20)) / 10.0f) - 1.0f;  /* -1..+1 */
        g_v.i_sq1k = (p10 < 5U) ? 1000 : -1000;
        g_v.i_sq5k = ((t & 1U) != 0U) ? 1000 : -1000;
        g_v.u_ramp = t % 1000U;

        /* x^32 + x^22 + x^2 + x^1 + 1（标准 maximal LFSR 的右移形式） */
        lfsr = (lfsr >> 1) ^ ((uint32_t)(-(int32_t)(lfsr & 1U)) & 0x80200003U);
        g_v.lfsr = lfsr;

        g_updates = t;

        /* 🚨 必须把 D-cache 写回：探针是用 **SBA（系统总线访问）** 读目标内存的，
         * **绕过 CPU 的 D-cache** —— 不写回的话探针读到的永远是 SRAM 里那份
         * "最初的 0"（实测踩过：同一时刻 OpenOCD 读到活的 163549，探针读回 0）。
         * OpenOCD 的内存访问走抽象命令、是**经 CPU** 的，所以它看得见新值；
         * 而这几行字一直待在 cache 里从没被逐出过。40 B 的写回开销可忽略。 */
        l1c_dc_writeback((uint32_t)(uintptr_t)&g_v, sizeof(g_v));
        l1c_dc_writeback((uint32_t)(uintptr_t)&g_updates, sizeof(g_updates));
        l1c_dc_writeback((uint32_t)(uintptr_t)&g_mchtmr_hz, sizeof(g_mchtmr_hz));

        if ((t & 0x1FFFU) == 0U)                      /* ~0.8 s 闪一次，证明在跑 */
        {
            board_led_toggle();
        }
    }
}

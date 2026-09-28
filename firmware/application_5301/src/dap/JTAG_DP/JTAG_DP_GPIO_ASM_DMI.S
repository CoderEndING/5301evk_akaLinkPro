/*
    JTAG DMI 扫描汇编（探针侧 RISC-V 引擎专用）—— akaLinkPro / HPM5301EVKLite

    引脚（JTAG 模式，PORT_JTAG_SETUP 已把引脚挂到 FGPIO，移位量由 -D 传入）：
        JTCK = PA27 / PA06      JTMS = PA28 / PA07
        JTDI = PA08 / PA05      JTDO = PA09 / PA04（输入）

    与 JTAG_DP_GPIO_ASM_45M.S 的关系：
      那个文件是 CMSIS-DAP 的通用 JTAG_Sequence（主机驱动用；每次调用只扫
      1~64 bit、TMS 固定，走通用分支并要把 TDI 打包成字节）。主机驱动下一次
      DMI 访问要调用它 6 次，C 侧开销约 1.25 us，占一次访问 4.1 us 的 30%。

      这里把**一次完整的 DMI 访问**收进单个函数：
        Run-Test/Idle -> Select-DR -> Capture-DR -> Shift-DR(41 bit)
        -> Exit1-DR -> Update-DR -> Run-Test/Idle
      41 bit 请求直接在寄存器里逐位右移，TDO 用 1 拍流水采样，C 侧只剩一次
      函数调用。

    时序：高电平相位的 nop 数（DMI_CAP_HIGH_NOP）决定 TDO 采样点，必须盖住
    电平转换 + 目标 tCK->TDO 的往返延迟。实测该往返远小于 JTAG_Sequence 里
    保守的 10 拍，这里默认给 4 拍，可再用 -D 调整后重测。
*/

.equ FGPIO_BASE,                (0x000C0000)
.equ FGPIO_DI_VAL_OFFSET,       (0x000)
.equ FGPIO_DO_VAL_OFFSET,       (0x100)
.equ FGPIO_DO_SET_OFFSET,       (0x104)

#ifndef JTAG_PIN_JTCK_SHIFT
#define JTAG_PIN_JTCK_SHIFT 27
#endif
#ifndef JTAG_PIN_JTMS_SHIFT
#define JTAG_PIN_JTMS_SHIFT 28
#endif
#ifndef JTAG_PIN_JTDI_SHIFT
#define JTAG_PIN_JTDI_SHIFT 8
#endif
#ifndef JTAG_PIN_JTDO_SHIFT
#define JTAG_PIN_JTDO_SHIFT 9
#endif
#ifndef DMI_CAP_HIGH_NOP
#define DMI_CAP_HIGH_NOP 4
#endif

.equ JTCK_OFFSET,               (1 << JTAG_PIN_JTCK_SHIFT)
.equ JTMS_OFFSET,               (1 << JTAG_PIN_JTMS_SHIFT)
.equ JTDI_OFFSET,               (1 << JTAG_PIN_JTDI_SHIFT)
.equ JTDI_SHIFT,                (JTAG_PIN_JTDI_SHIFT)
.equ JTDO_SHIFT,                (JTAG_PIN_JTDO_SHIFT)

/* 非移位状态（导航 / idle）TCK 高电平保持的 nop 数 */
#ifndef DMI_NAV_HIGH_NOP
#define DMI_NAV_HIGH_NOP 8
#endif
/* 非移位状态 TCK 低电平保持的 nop 数。**不能省**：TMS/TDI 是在拉低的同一
 * 次 DO_VAL 写里给出的，如果紧跟着就拉高，TMS 只在上升沿前 1 个周期才变化，
 * 目标端采样到的还是旧 TMS（TAP 会走错分支，实测表现为响应 op/addr 对、
 * 但读数据恒为 0）。 */
#ifndef DMI_NAV_LOW_NOP
#define DMI_NAV_LOW_NOP 8
#endif

/* 一个非移位时钟：给出 TMS/TDI -> 低电平保持 -> 拉高 -> 高电平保持 */
.macro DMI_NAV_CLOCK word
    sw      \word, FGPIO_DO_VAL_OFFSET(t0)
.rept DMI_NAV_LOW_NOP
    nop
.endr
    sw      t1, FGPIO_DO_SET_OFFSET(t0)
.rept DMI_NAV_HIGH_NOP
    nop
.endr
.endm

/* 移位位（TMS 固定为 0，所以不需要叠加 TMS）：拉低并给出 TDI -> 处理上一拍
 * 采样 -> 拉高 -> 采样。
 *
 * 试过把"处理上一拍采样"的 4 条指令挪到高电平的死等里（低相位只剩 2 拍），
 * 速率确实从 1190 涨到 1386 KB/s（+16%），但**自检恒定失败**（cap=4 和 cap=6
 * 拿到的是同一个错误校验和 0x11D9A168，说明是确定性的逻辑错而不是余量不足）：
 * TCK 拉低到拉高之间只剩一条 srli（约 2 拍 ≈ 5 ns），**TDI 建立时间不够**，
 * 目标每位采到的都是上一位。结论：低相位（TDI/TMS 建立）和高相位（TDO 往返）
 * 各需 ~8 拍，这个结构下 TCK 周期下限就是 18 拍，实测 16.3 MHz。
 * 要再往上只能改成"上一位的高相位里就把下一位的 TDI 用 DO_SET/DO_CLR 摆好"，
 * 那样低相位只剩一次拉低 —— 理论上限 25 MHz（目标规格）。
 *
 * \out : TDO 累加器（每拍右移，新位进 bit31）
 * \in  : TDI 位来源（每拍右移，bit0 是下一位） */
.macro DMI_SHIFT_BIT out, in
    andi    t4, \in, 1
    slli    t4, t4, JTDI_SHIFT
    sw      t4, FGPIO_DO_VAL_OFFSET(t0)     # --- TCK 拉低 + TDI（TMS=0）
    srli    \in, \in, 1
    bexti   a6, a5, JTDO_SHIFT              # 上一拍的 TDO
    srli    \out, \out, 1
    slli    a6, a6, 31
    or      \out, \out, a6
    sw      t1, FGPIO_DO_SET_OFFSET(t0)     # --- TCK 拉高
.rept DMI_CAP_HIGH_NOP
    nop
.endr
    lw      a5, FGPIO_DI_VAL_OFFSET(t0)     # 采样 TDO
.endm

/* 冲刷 1 拍流水里剩下的最后一位采样 */
.macro DMI_FLUSH_BIT out
    bexti   a6, a5, JTDO_SHIFT
    srli    \out, \out, 1
    slli    a6, a6, 31
    or      \out, \out, a6
.endm

.section .fast, "ax"
.align  2
.global JTAG_DMI_Scan
.type   JTAG_DMI_Scan, @function

/* uint32_t JTAG_DMI_Scan(uint32_t idle, uint32_t dr_lo, uint32_t dr_mid,
 *                        uint32_t dr_hi, uint32_t *tdo_hi)
 *   a0 = Run-Test/Idle 周期数（0..255）
 *   a1 = DMI 请求 bit[31:0]
 *   a2 = DMI 请求 bit[39:32]（8 位）
 *   a3 = DMI 请求 bit40（bit0 有效）
 *   a4 = 存放 TDO bit[40:32] 的地址（低 9 位有效）
 * 返回 a0 = TDO bit[31:0]
 */
JTAG_DMI_Scan:
    addi    sp, sp, -16
    sw      s0, 0(sp)

    li      t0, FGPIO_BASE
    li      t1, JTCK_OFFSET
    li      t2, JTMS_OFFSET
    li      t5, JTDI_OFFSET
    or      t6, t2, t5                  # TMS=1 + TDI=1（导航用）
    mv      s0, a4                      # &tdo_hi

    # ---------------- Run-Test/Idle（TMS=0, TDI=1） ----------------
    beqz    a0, .Ldmi_nav
.Ldmi_idle:
    DMI_NAV_CLOCK t5
    addi    a0, a0, -1
    bnez    a0, .Ldmi_idle

    # ---------------- RTI -> Select-DR -> Capture-DR -> Shift-DR ----------------
.Ldmi_nav:
    DMI_NAV_CLOCK t6                        # Select-DR-Scan（TMS=1）
    DMI_NAV_CLOCK t5                        # Capture-DR（TMS=0）
    DMI_NAV_CLOCK t5                        # Shift-DR（TMS=0）

    # ---------------- 移位 32 bit（TMS=0，捕获 TDO） ----------------
    mv      a0, x0                          # TDO bit[31:0]
    mv      a5, x0                          # 上一拍 DI 整字（1 拍流水）
    li      t3, 32
.Ldmi_lo32:
    DMI_SHIFT_BIT a0, a1
    addi    t3, t3, -1
    bnez    t3, .Ldmi_lo32
    DMI_FLUSH_BIT a0

    # ---------------- 移位 8 bit（TMS=0，TDO bit[39:32]） ----------------
    mv      a4, x0
    mv      a5, x0
    li      t3, 8
.Ldmi_hi8:
    DMI_SHIFT_BIT a4, a2
    addi    t3, t3, -1
    bnez    t3, .Ldmi_hi8
    DMI_FLUSH_BIT a4
    /* 累加器是"每次右移、新位进 bit31"，只有正好 32 位时才自然对齐到
     * bit[31:0]；8 位会停在 bit[31:24]，右移回来。 */
    srli    a4, a4, 24

    # ---------------- bit40（TMS=1，Exit1-DR） ----------------
    mv      a5, x0
    andi    t4, a3, 1
    slli    t4, t4, JTDI_SHIFT
    /* 只能用 TMS 预设 t2：t6 里带了导航用的 TDI=1，叠加上去会把请求的
     * bit40 强制成 1（等于把地址或上 0x40，落到非法寄存器上）。 */
    or      t4, t4, t2
    sw      t4, FGPIO_DO_VAL_OFFSET(t0)
.rept DMI_NAV_LOW_NOP
    nop
.endr
    sw      t1, FGPIO_DO_SET_OFFSET(t0)
.rept DMI_CAP_HIGH_NOP
    nop
.endr
    lw      a5, FGPIO_DI_VAL_OFFSET(t0)
    bexti   a6, a5, JTDO_SHIFT
    slli    a6, a6, 8
    or      a4, a4, a6                      # TDO bit40 -> bit8

    # ---------------- Update-DR -> Run-Test/Idle ----------------
    DMI_NAV_CLOCK t6                        # Update-DR（TMS=1）
    DMI_NAV_CLOCK t5                        # Run-Test/Idle（TMS=0）

    sw      a4, 0(s0)
    lw      s0, 0(sp)
    addi    sp, sp, 16
    ret

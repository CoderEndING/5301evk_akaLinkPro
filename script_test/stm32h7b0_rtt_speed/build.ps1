<#
  STM32H7B0 RTT 吞吐测试固件（**寄存器版**：不依赖任何 SDK / HAL）

    pwsh -File build.ps1
    pwsh -File build.ps1 -SlowClock     # 保命档：只跑 HSI 64MHz（完全不碰 PLL/VOS）
    pwsh -File build.ps1 -Clean

  依赖：arm-none-eabi-gcc 在 PATH 里（本机在 E:\Share\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin）
  产物：build/fw.elf + fw.bin + fw.hex，并复制一份到目录根 `fw.elf`（网页里直接载入/下载用）

  ⚠️ 本目录是**精简版**：只保留"一个外部文件都不依赖"的寄存器版。
     带 HAL/SDK 的完整版（外加 -Minimal 的两种档位说明）在网页工具仓库
     `web-serial-rtt-tools/tools/target-firmware/stm32h7b0_rtt_speed/`；两边的 src/、ld/、
     segger_rtt/ 是**同一份**，只有本脚本少了几条分支 —— 改源码请两边一起改。
#>
param([switch]$Clean, [switch]$SlowClock)

$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$bdir  = Join-Path $root 'build'

$gcc = (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue).Source
if (-not $gcc){
  $guess = 'E:\Share\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-gcc.exe'
  if (Test-Path $guess){ $gcc = $guess } else { throw 'arm-none-eabi-gcc 不在 PATH 里' }
}
$bin     = Split-Path -Parent $gcc
$objcopy = Join-Path $bin 'arm-none-eabi-objcopy.exe'
$size    = Join-Path $bin 'arm-none-eabi-size.exe'

if ($Clean -and (Test-Path $bdir)) { Remove-Item $bdir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $bdir | Out-Null

$sources = @(
  (Join-Path $root 'src\main.c'),
  (Join-Path $root 'src\startup.c'),
  (Join-Path $root 'segger_rtt\SEGGER_RTT.c')
)
$elf = Join-Path $bdir 'fw.elf'

# 参数一律用数组 splat：PowerShell 会把 -specs=nano.specs 按点号拆成两段
# （"-specs=nano" + ".specs"），直接导致 cannot read spec file 'nano'。
$cflags = @(
  '-mcpu=cortex-m7', '-mthumb', '-Os', '-g3',
  '-mfpu=fpv5-d16', '-mfloat-abi=hard',
  '-ffunction-sections', '-fdata-sections', '-fno-common',
  '-Wall', '-Wextra', '-Wno-unused-parameter',
  "-I$root\src", "-I$root\segger_rtt",
  "-T$root\ld\stm32h7b0.ld",
  '-nostartfiles', '-specs=nano.specs', '-specs=nosys.specs',
  '-Wl,--gc-sections', "-Wl,-Map=$bdir\fw.map"
)
if ($SlowClock){ $cflags += '-DHSI_64MHZ_ONLY=1' }

& $gcc @cflags @sources -o $elf
if ($LASTEXITCODE -ne 0) { throw "编译失败 (exit $LASTEXITCODE)" }

& $objcopy -O binary $elf (Join-Path $bdir 'fw.bin')
& $objcopy -O ihex   $elf (Join-Path $bdir 'fw.hex')
& $size $elf

# 复制到目录根：仓库里"给用户直接下载/载入"的那份就是它
Copy-Item -Force $elf (Join-Path $root 'fw.elf')

Write-Output ""
Write-Output ("时钟    ： {0}" -f $(if ($SlowClock) { 'HSI 64MHz（保命档，完全不碰 PLL/VOS）' } else { 'HSE 25MHz→PLL1 280MHz（VOS0）；无晶振自动退 HSI→PLL' }))
Write-Output ("产物    ： {0}" -f $elf)
Write-Output ("入库    ： {0} ({1} KB)" -f (Join-Path $root 'fw.elf'), [int]((Get-Item (Join-Path $root 'fw.elf')).Length / 1024))
Write-Output ""
Write-Output "下一步：烧进去，然后在网页「RTT Viewer」里载入目录根的 fw.elf（页面自动取 _SEGGER_RTT 地址）"

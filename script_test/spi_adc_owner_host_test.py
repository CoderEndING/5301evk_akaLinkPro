"""Compile the production SPI/ADC shared-buffer ownership helpers and prove two
properties that a host cannot recover from otherwise:

  1. a bridge that is *really* in use (enabled / mid-frame / already owned by ADC)
     always blocks takeover  -> mutual exclusion stays intact;
  2. stale bookkeeping left behind by a closed bridge (hardware re-init pending,
     CS, drain counters, ring counts, in-flight counters, request flags) must NOT
     block takeover forever -> the ADC reclaims the buffers instead.

Property 2 is the regression for the 2026-10-06 field failure: with the bridge
disabled and the ADC idle the probe still reported flags=2, so the web refused to
start ADC and no host command could clear it.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root / 'firmware/application_5301/src/spi_bridge/spi_bridge.c').read_text(encoding='utf-8')
start = text.index('static void sb_cs_release(void);', text.index('#define s_adc_owner'))
end = text.index('/* ============================== 小工具', start)
helper = text[start:end]

# Every identifier the extracted helpers touch, in the production storage class.
bytes_ = ['s_adc_owner', 's_out_inflight', 's_enabled', 's_in_inflight', 's_in_used', 's_out_used',
          's_pkt_active', 's_hw_req', 's_reset_req', 's_usb_reset_req', 's_abort_req',
          's_drain_reads', 's_cs_asserted']
words = ['s_pkt_off', 's_pkt_len', 's_rst_state', 's_delay_active',
         's_out_gen', 's_in_gen', 's_out_w', 's_out_r', 's_in_w', 's_in_r']

# Flags that mean "the bridge really owns the shared buffers right now".
live = ['s_enabled', 's_pkt_active']
# Leftovers from a closed bridge: handover must reclaim them, never refuse on them.
stale = ['s_out_inflight', 's_in_inflight', 's_in_used', 's_out_used', 's_hw_req',
         's_reset_req', 's_usb_reset_req', 's_abort_req', 's_drain_reads', 's_cs_asserted']

source = '#include <stdint.h>\n#include <assert.h>\n'
source += 'static uint8_t ' + ','.join(bytes_) + ';\n'
source += 'static uint32_t ' + ','.join(words) + ';\n'
source += 'static uint8_t s_out_buf[32][512],s_in_buf[16][512];\n'
source += 'static unsigned cs_release_calls;\n'
source += 'static void sb_cs_release(void){cs_release_calls++;s_cs_asserted=0U;}\n'
source += helper
source += 'int main(void){uint32_t *capture=0;uint8_t *transmit=0;\n'
source += 'assert(!spi_bridge_adc_flags());assert(spi_bridge_adc_claim(&capture,&transmit));\n'
source += 'assert(capture==(uint32_t*)&s_out_buf[0][0] && transmit==&s_in_buf[0][0]);\n'
source += 'assert(spi_bridge_adc_flags()==2);assert(!spi_bridge_adc_claim(&capture,&transmit));\n'
source += 'spi_bridge_adc_release();assert(!spi_bridge_adc_flags());\n'
source += 'assert(!cs_release_calls);\n'
for flag in live:
    source += f'{flag}=1;assert(spi_bridge_adc_flags()&2);assert(!spi_bridge_adc_claim(&capture,&transmit));{flag}=0;\n'
source += 'assert(!spi_bridge_adc_flags());\n'
source += 's_out_inflight=1;assert(spi_bridge_adc_flags()==1);assert(spi_bridge_adc_claim(&capture,&transmit));\n'
source += 'assert(spi_bridge_adc_flags()==2);spi_bridge_adc_release();assert(!spi_bridge_adc_flags());\n'
for flag in stale:
    source += (f'{flag}=1;assert(spi_bridge_adc_claim(&capture,&transmit));'
               f'assert(!{flag});spi_bridge_adc_release();assert(!spi_bridge_adc_flags());\n')
source += 'assert(cs_release_calls==1);\n'
source += 's_hw_req=1;s_reset_req=1;s_usb_reset_req=1;s_abort_req=1;s_drain_reads=1;'
source += 's_in_used=1;s_out_used=1;s_in_inflight=1;s_out_inflight=1;s_pkt_active=0;'
source += 'uint32_t og=s_out_gen,ig=s_in_gen;\n'
source += 'assert(spi_bridge_adc_claim(&capture,&transmit));\n'
source += 'assert(s_out_gen==og+1 && s_in_gen==ig+1);\n'
source += 'assert(!s_in_used&&!s_out_used&&!s_in_inflight&&!s_out_inflight&&!s_pkt_off&&!s_pkt_len);\n'
source += 'assert(!s_hw_req&&!s_reset_req&&!s_usb_reset_req&&!s_abort_req&&!s_drain_reads);\n'
source += 'assert(!s_out_r&&!s_out_w&&!s_in_r&&!s_in_w&&!s_delay_active&&!s_rst_state);\n'
source += 'spi_bridge_adc_release();return 0;}\n'

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(source, encoding='utf-8')
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('SPI/ADC ownership: enabled/mid-frame/ADC-owned still block; hardware-reinit, CS, drain, '
      'ring and in-flight leftovers are reclaimed with a generation bump PASS')

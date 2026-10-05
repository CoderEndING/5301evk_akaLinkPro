"""Run production SPI buffer claim helpers; no mocked ownership decision."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
text = (root / 'firmware/application_5301/src/spi_bridge/spi_bridge.c').read_text()
start = text.index('uint8_t spi_bridge_adc_flags(void)', text.index('#define s_adc_owner'))
end = text.index('/* ============================== 小工具', start)
helper = text[start:end]
flags = ['s_adc_owner','s_out_inflight','s_enabled','s_in_inflight','s_in_used','s_out_used','s_pkt_active',
         's_hw_req','s_reset_req','s_usb_reset_req','s_abort_req','s_drain_reads','s_cs_asserted']
source = '#include <stdint.h>\n#include <assert.h>\n'
source += 'static uint8_t '+','.join(flags)+';\n'
source += 'static uint8_t s_out_buf[32][512],s_in_buf[16][512];\n'+helper
source += 'int main(void){uint32_t *capture=0;uint8_t *transmit=0;\n'
for flag in flags:
    mask = 1 if flag == 's_out_inflight' else 2
    source += f'{flag}=1;assert(spi_bridge_adc_flags()&{mask});assert(!spi_bridge_adc_claim(&capture,&transmit));{flag}=0;\n'
source += '''assert(!spi_bridge_adc_flags());assert(spi_bridge_adc_claim(&capture,&transmit));
assert(capture==(uint32_t*)&s_out_buf[0][0] && transmit==&s_in_buf[0][0]);
assert(spi_bridge_adc_flags()==2);assert(!spi_bridge_adc_claim(&capture,&transmit));
spi_bridge_adc_release();assert(!spi_bridge_adc_flags());
assert(spi_bridge_adc_claim(&capture,&transmit));spi_bridge_adc_release();return 0;}'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.c').write_text(source)
    subprocess.run([os.environ.get('CC','gcc'),'-std=c11','-Wall','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('SPI/ADC production claim: every DMA/work/CS owner blocks takeover, exact shared buffer addresses, release/reclaim PASS')

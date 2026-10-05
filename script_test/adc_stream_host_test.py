"""Production periodic engine + ADC USB producer, with a virtual USB controller."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'firmware/application_5301/src'
stub = r'''
#include <stdint.h>
#include <string.h>
#define USB_NOCACHE_RAM_SECTION
#define USB_MEM_ALIGNX
#define ADC_IN_EP 0x8c
int usbd_ep_start_write(uint8_t,uint8_t,uint8_t*,uint32_t);
'''
test = r'''
#define ADC_STREAM_HOST 1
#define main periodic_regression
#include "bus_periodic_test.c"
#undef main
#include "adc_stream.h"
#include "analog_bridge.h"
static uint8_t *dma; static uint32_t dma_len; static unsigned sends;
int usbd_ep_start_write(uint8_t bus,uint8_t ep,uint8_t *p,uint32_t len) {
 assert(bus==0 && ep==0x8c && !dma && len<=510 && len<512);
 dma=p;dma_len=len;sends++;return 0;
}
static void complete(void) { assert(dma);dma=0;adc_stream_complete(); }
static void adc(unsigned count) {
 memset(req,0,sizeof(req));req[1]=13;req[5]=BP_ADC;req[8]=6;
 uint8_t p[]={BP_ADC,0,2,0,3,16};memcpy(req+9,p,6);
 assert(command(BP_PUT)==BP_OK);start(0,1,count);run();
}
int main(void) {
 uint32_t token,old;reset();assert(adc_stream_open(&token)==ANALOG_STATE);
 adc_stream_reset(1);assert(!adc_stream_open(&token));old=token;
 adc(3);assert(dma_len==42 && dma[5]==1 && dma[6]==1 && r32(dma+8)==token);
 assert(bus_periodic_queued()==1);assert(command(BP_READ)==BP_BUSY);assert(command(BP_ACK)==BP_BUSY);
 uint8_t saved[512];memcpy(saved,dma,dma_len);advance(2);
 assert(!memcmp(saved,dma,dma_len) && bus_periodic_queued()==3);
 assert(command(BP_CLEAR)==BP_BUSY);assert(adc_stream_close()==ANALOG_BUSY);
 complete();assert(dma_len==68 && dma[6]==2);complete();
 assert(dma_len==16 && dma[5]==2 && !bus_periodic_queued());complete();
 assert(!dma && !adc_stream_close());assert(!adc_stream_open(&token)&&token!=old);
 assert(command(BP_CLEAR)==BP_OK);adc(0);advance(40);
 assert(bus_periodic_fault()==BP_OVERFLOW && bus_periodic_queued()==32 && !bus_periodic_owns(BP_ADC));
 complete();assert(dma_len==510 && dma[6]==19 && r32(dma+12)==BP_OVERFLOW);complete();
 assert(dma[6]==12);complete();assert(dma[5]==2);complete();assert(!adc_stream_close());
 assert(command(BP_CLEAR)==BP_OK);assert(!adc_stream_open(&token));adc_stream_end();
 assert(dma_len==16 && dma[5]==2);complete();assert(!adc_stream_close());
 assert(command(BP_CLEAR)==BP_OK);assert(!adc_stream_open(&token));adc(0);
 /* Hardware cancels DMA before RESET notification; no old completion can acknowledge a new queue. */
 dma=0;adc_stream_reset(0);bus_periodic_reset();poll();
 assert(!bus_periodic_queued() && !adc_stream_enabled());adc_stream_complete();assert(!dma);
 adc_stream_reset(1);assert(!adc_stream_open(&token));adc_stream_end();complete();assert(!adc_stream_close());
 printf("ADC production Bulk: finite/count, batching, DMA ownership, HID exclusion, overflow, END, restart/reset PASS (%u transfers)\n",sends);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path/'usb_composite.h').write_text(stub)
    (path/'test.c').write_text(test)
    exe = path/'test'
    flags = ['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.getenv('BUS_HOST_SANITIZE') else []
    subprocess.run([os.getenv('CC','gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror',*flags,
        '-Wno-return-type',f'-I{path}',f'-I{root / "script_test/host"}',
        *[f'-I{src / p}' for p in ['bus_periodic','i2c_bridge','spi_bridge','scope','rtt','analog_bridge']],
        str(src/'bus_periodic/bus_periodic.c'),str(src/'analog_bridge/adc_stream.c'),str(path/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

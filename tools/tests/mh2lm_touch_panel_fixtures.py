"""OS/MMIO and I2C wire adapters for the actual C TouchComm worker.
The state machine, IRQ handler, HID release, and TCM protocol remain production
code. Test-only helpers model reboot/worker entry and independent firmware I/O.
"""
import re

C_OS = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#define ARM64_BOARD_CONFIG_SWEET 1
#define SWEET_DEV_TOUCH_TCM 1
#define SWEET_DEV_TOUCH_NORTH_PA 0x03000000
#define SWEET_DEV_FB_W 1080
#define SWEET_DEV_FB_H 2340
#define __unused __attribute__((unused))
typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
typedef void* thread_t;
typedef int wait_result_t;
typedef int kern_return_t;
#define THREAD_UNINT 0
#define THREAD_AWAKENED 1
#define THREAD_CONTINUE_NULL NULL
#define NSEC_PER_MSEC 1000000
#define KERN_SUCCESS 0
extern uint64_t touchTime(void);
extern void touchSleep(uint64_t);
extern void touchAssertWait(void*);
extern void touchClearWait(void);
extern void touchBlock(void);
extern void touchWake(void*);
extern uint32_t touchRead(uintptr_t,uint32_t);
extern void touchWrite(uintptr_t,uint32_t,uint32_t);
extern int touchBusRead(uint8_t*,size_t);
extern int touchBusWrite(const uint8_t*,size_t);
extern void touchHID(int,int,int,int);
extern void touchLog(const char*,...);
static uint64_t mach_absolute_time(void){return touchTime();}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t*n){*n=t;}
static void clock_interval_to_deadline(unsigned ms,uint64_t unit,uint64_t*d){*d=touchTime()+ms*unit;}
static int assert_wait(void*e,int x){(void)x;touchAssertWait(e);return 0;}
static int assert_wait_deadline(void*e,int x,uint64_t d){(void)x;touchSleep(d);touchAssertWait(e);return 0;}
static void*current_thread(void){return NULL;}
static void clear_wait(void*t,int x){(void)t;(void)x;touchClearWait();}
static void thread_block(void*p){(void)p;touchBlock();}
static void thread_wakeup(void*e){touchWake(e);}
static void delay(unsigned us){touchSleep(touchTime()+us*1000ULL);}
static int PE_parse_boot_argn(const char*k,void*v,size_t n){(void)k;(void)v;(void)n;return 0;}
static int kernel_thread_start(void(*f)(void*,int),void*a,thread_t*t){(void)f;(void)a;*t=NULL;return -1;}
static void thread_deallocate(thread_t t){(void)t;}
vm_offset_t ml_io_map(vm_offset_t p,vm_size_t n){(void)n;return p;}
kern_return_t thread_terminate(thread_t t){(void)t;return 0;}
void sweet_hid_touch(int f,int d,int x,int y){touchHID(f,d,x,y);}
#define SWEET_LOG_INFO touchLog
#define SWEET_LOG_WARN touchLog
#define SWEET_LOG_ERROR touchLog
#define SWEET_LOG_DEBUG touchLog
'''
C_HELPERS = r'''
void tcFixtureReboot(void){
 ts_panel_owner=ts_panel_serial=ts_panel_frame=0;ts_panel_live=ts_panel_cancel=0;
 ts_panel_servicing=false;ts_panel_deadline=0;ts_panel_pin_cfg=0;
 const uintptr_t b[]={0x03100000,0x03500000,0x03900000,0x03d00000};
 for(unsigned n=0;n<4;++n)ts_tiles[n]=b[n];ts_gicd=TS_GICD_PA;
 ts_irq_ready=true;ts_irq_hits=ts_irq_probe_hits=0;ts_irq_state=1;
 ts_irq_empty=ts_irq_no_progress=0;
 memset(&tcm,0,sizeof(tcm));tcm.ready=1;tcm.max_x=1080;tcm.max_y=2340;
 memset(ts_now,0,sizeof(ts_now));memset(ts_prev,0,sizeof(ts_prev));
 ts_panel_publish(0,SWEET_TOUCH_RUNNING);ts_panel_live=1;
}
void tcFixtureOnline(unsigned online){__atomic_store_n(&ts_panel_live,online,__ATOMIC_RELEASE);}
unsigned tcFixtureStep(void){return ts_panel_step();}
void tcFixtureWait(void){ts_panel_wait();}
void tcFixtureIRQWait(void){ts_irq_wait(__atomic_load_n(&ts_irq_hits,__ATOMIC_ACQUIRE));}
unsigned tcFixtureIRQState(void){return __atomic_load_n(&ts_irq_state,__ATOMIC_ACQUIRE);}
uint64_t tcFixtureFrame(void){return __atomic_load_n(&ts_panel_frame,__ATOMIC_ACQUIRE);}
uint64_t tcFixtureOwner(void){return __atomic_load_n(&ts_panel_owner,__ATOMIC_ACQUIRE);}
void tcFixtureContact(unsigned finger){ts_prev[finger].active=1;ts_px[finger]=12;ts_py[finger]=34;}
void tcFixtureTokenLimit(void){ts_panel_frame=(TS_PANEL_LIMIT<<4)|SWEET_TOUCH_RUNNING;}
void tcFixtureSessionLimit(void){ts_panel_serial=TS_PANEL_LIMIT;}
'''

def actual_worker(root):
    text = (root/'bsd/dev/sweet_touch_tcm.c').read_text()
    abi = (root/'bsd/dev/sweet_touch_panel.h').read_text()
    text = re.sub(r'^#include.*\n', '', text, flags=re.M)
    abi = re.sub(r'^#include.*\n', '', abi, flags=re.M)
    # Only low-level OS, MMIO and bus boundaries are substituted.
    replacements = {
        'ts_rd': 'return touchRead(base,off);',
        'ts_wr': 'touchWrite(base,off,v);',
        'ts_gic_rd64': 'return (uint64_t)touchRead(ts_gicd,off)|((uint64_t)touchRead(ts_gicd,off+4)<<32);',
        'ts_gic_wr64': 'touchWrite(ts_gicd,off,(uint32_t)value);touchWrite(ts_gicd,off+4,(uint32_t)(value>>32));',
        'ts_i2c_write': 'if(ts_panel_interrupted())return ECANCELED;return touchBusWrite(tx,n);',
        'ts_i2c_read': 'if(ts_panel_interrupted())return ECANCELED;return touchBusRead(rx,n);',
    }
    for name, body in replacements.items():
        m = re.search(r'\n'+name+r'\([^\n]*\)\n\{', text)
        assert m, name
        brace = m.end()-1
        end, depth = brace+1, 1
        while depth:
            depth += (text[end] == '{')-(text[end] == '}')
            end += 1
        text = text[:brace]+'{ '+body+' }'+text[end:]
    return C_OS+abi+text+C_HELPERS

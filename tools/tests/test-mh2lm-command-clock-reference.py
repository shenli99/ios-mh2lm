#!/usr/bin/env python3
"""Execute pinned LG host_transfer with the actual clock manager and callbacks.
Usage: test-mh2lm-command-clock-reference.py LINEAGE_ROOT [EVIDENCE_DIR]
Command engine, wire transfer, OS and physical clocks are fixture peers.
"""
from pathlib import Path
import json,os,subprocess,sys,tempfile
if len(sys.argv) not in (2,3):sys.exit(__doc__)
p=Path(__file__).with_name('test-mh2lm-lineage-clock-reference.py')
s=p.read_text().split('with tempfile.TemporaryDirectory(')[0]
# Same exact-source/pinned-HEAD extraction, with host_transfer's return type.
s=s.replace('(?:int|void)', '(?:int|void|ssize_t)')
n={'__file__':str(p),'__name__':'host_clock_reference'}
exec(compile(s,str(p),'exec'),n)
code=n['code'].split('static void expect(')[0]
code=code.replace('typedef uint32_t u32;', '''typedef uint32_t u32;
typedef long ssize_t;
struct mipi_dsi_host {int unused;};
struct mipi_dsi_msg {unsigned flags,ctrl;};
#define MIPI_DSI_MSG_UNICAST 1u
#define DSI_CTRL_CMD_FETCH_MEMORY 1u
#define atomic_read(p) (*(p))
#define to_dsi_display(p) container_of(p,struct dsi_display,host)
''')
code=code.replace('struct dsi_panel {bool', 'struct dsi_panel {int esd_recovery_pending;bool')
code=code.replace('struct dsi_display {struct', 'struct dsi_display {struct mipi_dsi_host host;void*dsi_clk_handle;void*tx_cmd_buf;struct')
code+=r'''
static int fault=0,engine=0;
static struct dsi_clk_client_info*current;
static int dsi_display_clk_ctrl(void*h,enum dsi_clk_type t,enum dsi_clk_state state){
 event(state==DSI_CLK_ON?"clock-on":"clock-off");
 if(fault==1&&state==DSI_CLK_ON)return -EIO;
 current=h;return dsi_clk_req_state(h,t,state);
}
static int dsi_display_cmd_engine_enable(struct dsi_display*d){
 event("engine-on");assert(current->core_refcount&&current->link_refcount);
 if(fault==2)return -EIO;engine=1;return 0;
}
static int dsi_display_cmd_engine_disable(struct dsi_display*d){event("engine-off");assert(engine);engine=0;return 0;}
static int dsi_host_alloc_cmd_tx_buffer(struct dsi_display*d){event("allocate");if(fault==3)return -ENOMEM;d->tx_cmd_buf=d;return 0;}
static int dsi_ctrl_cmd_transfer(void*c,const struct mipi_dsi_msg*m,unsigned f){event("transfer");assert(engine&&current->core_refcount&&current->link_refcount);return fault==4?-EIO:0;}
static int dsi_display_broadcast_cmd(struct dsi_display*d,const struct mipi_dsi_msg*m){assert(false);return 0;}
'''
code+=n['extract'](n['display'],'dsi_display_wake_up')
code+=n['extract'](n['display'],'dsi_host_transfer')
# Reuse only construction of the independent manager/client/clock peers.
code+=n['code'].split('static void expect(',1)[1].split(' // Ordinary blank:',1)[0]
code=code.replace('const char*s){if(strcmp(events,s))','static void expect(const char*s){if(strcmp(events,s))',1)
code+=r'''
 display.dsi_clk_handle=&dsi;display.tx_cmd_buf=&display;
 struct mipi_dsi_msg msg={0};
 for(unsigned owner=0;owner<2;++owner){
  if(owner){assert(!dsi_clk_req_state(&mdp,DSI_ALL_CLKS,DSI_CLK_ON));assert(!dsi_clk_req_state(&dsi,DSI_ALL_CLKS,DSI_CLK_OFF));}
  assert(!events[0]);
  for(unsigned packet=0;packet<17;++packet){
   unsigned core=dsi.core_refcount,link=dsi.link_refcount;
   assert(!dsi_host_transfer(&display.host,&msg));
   expect("clock-on engine-on transfer engine-off clock-off ");
   assert(dsi.core_refcount==core&&dsi.link_refcount==link&&!engine);
   assert(m.core_clk_state==DSI_CLK_ON&&m.link_clk_state==DSI_CLK_ON);
  }
  for(fault=1;fault<=4;++fault){
   unsigned core=dsi.core_refcount,link=dsi.link_refcount;
   display.tx_cmd_buf=fault==3?NULL:&display;
   assert(dsi_host_transfer(&display.host,&msg)<0);
   if(fault==1)expect("clock-on ");
   if(fault==2)expect("clock-on engine-on clock-off ");
   if(fault==3)expect("clock-on engine-on allocate engine-off clock-off ");
   if(fault==4)expect("clock-on engine-on transfer engine-off clock-off ");
   assert(dsi.core_refcount==core&&dsi.link_refcount==link&&!engine);
  }
  fault=0;display.tx_cmd_buf=&display;
 }
 panel.esd_recovery_pending=1;assert(!dsi_host_transfer(&display.host,&msg));assert(!events[0]);
 puts("PASS: exact LG host_transfer/wake_up plus clock-manager call chain; 17 nested commands with DSI or MDP outer owner; all four error exits; engine inside clock lease; shared clocks stay on. Linux releases even on transfer error; XNU intentionally retains uncertain hardware ownership.");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-command-clock-reference-') as tmp:
 tmp=Path(tmp);(tmp/'reference.c').write_text(code)
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11','-g','-fsanitize=address,undefined',str(tmp/'reference.c'),'-o',str(tmp/'reference')],check=True)
 output=subprocess.check_output([str(tmp/'reference')],text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
 print(output,end='')
 if len(sys.argv)==3:
  dest=Path(sys.argv[2]);dest.mkdir(parents=True,exist_ok=True)
  (dest/'command-clock-reference.c').write_text(code)
  (dest/'command-clock-reference.json').write_text(json.dumps(dict(pin=n['pin'],sources=n['sources'],functions=n['extracted'],static_audit=n['static_audit'],scope=__doc__,output=output),indent=2)+'\n')

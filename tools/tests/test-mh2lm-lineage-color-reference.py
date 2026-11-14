#!/usr/bin/env python3
"""Execute pinned LG color mappers, mode-set and vendor transfer unchanged.
Usage: test-mh2lm-lineage-color-reference.py LINEAGE_ROOT EVIDENCE_DIR
Outputs a wire oracle for the current SDR Auto/preset2/RGB0 policy. Other
branches are exercised to establish that this is not a universal vendor table.
DRM/CCF, MMIO and iOS color policy are not simulated by this reference test.
"""
from pathlib import Path
import hashlib, json, os, re, subprocess, sys
if len(sys.argv) != 3:
    sys.exit(__doc__)
root, out = map(lambda s: Path(s).resolve(), sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
pin = '2e897dddf49a4c3e85b2ff77fdf7c70f860398dd'
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip() == pin
sources, functions = {}, []
def read(path):
    data = (root/path).read_text()
    assert subprocess.check_output(['git', 'show', f'{pin}:{path}'], cwd=root) == data.encode()
    sources[path] = hashlib.sha256(data.encode()).hexdigest()
    return data
def extract(path, name):
    data = read(path)
    m = re.search(r'^(?:static )?(?:int|void)\s+'+name+r'\s*\([^;{}]*\)\s*\{', data, re.M)
    assert m, name
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', lambda m: ' '*len(m[0]), data, flags=re.S)
    pos, depth = m.end(), 1
    while depth:
        depth += (masked[pos] == '{') - (masked[pos] == '}')
        pos += 1
    functions.append(name)
    return data[m.start():pos]+'\n'
lg = 'drivers/gpu/drm/msm/lge/'
ops = lg+'lge_ddic_ops_r66456_tianma_md.c'
panel = read(lg+'lge_dsi_panel.c')
assert 'panel->lge.cm_preset_step = 2;' in panel
assert 'panel = kzalloc(sizeof(*panel), GFP_KERNEL)' in read('drivers/gpu/drm/msm/dsi-staging/dsi_panel.c')
assert '.lge_set_screen_mode = lge_set_screen_mode_r66456' in read(ops)
assert '.lge_bc_dim_set = NULL' in read(ops)
assert 'lge_dsi_panel_mode_set(panel);' in panel
mode_call = panel[panel.index('int dsi_panel_set_nolp('):panel.index('int dsi_panel_set_nolp(')+2500]
assert 'lp_mode == SDE_MODE_DPMS_ON && last_panel_power_mode == SDE_MODE_DPMS_OFF' in mode_call
assert 'goto mode_set;' in mode_call and 'mode_set:\n\tlge_dsi_panel_mode_set(panel);' in mode_call
# The actual vendor transfer assigns LP only from the command-set state.
read(lg+'cm/lge_color_manager.h')
properties = ['lge,color-mode-cmds-dummy', 'lge,color-mode-set', 'lge,rgb-lut',
              'lge,custom-rgb-hue-lut', 'lge,saturation-lut',
              'lge,mdss-dsi-sharpness-command', 'lge,sharpness-lut']
records, arrays, setups = [], [], []
for ident, profile in enumerate(('pp1', 'dv1', 'dv2', 'pv'), 1):
    path = 'arch/arm64/boot/dts/lge/sm8150_common/settings_panel/dsi-panel-r66456-'+profile+'-tianmamd-fhd-cmd.dtsi'
    data = read(path)
    common = read('arch/arm64/boot/dts/lge/sm8150_common/sm8150_common-panel.dtsi')
    assert 'lge,use-color-manager;' in data
    assert 'of_property_read_bool(of_node, "lge,ddic-register-backup")' in panel
    assert 'lge,ddic-register-backup' not in data+common
    for prop in (properties[0], properties[5]):
        assert prop+'-state = "dsi_hs_mode";' in data
    sets = []
    for k, prop in enumerate(properties):
        m = re.search(re.escape(prop)+r'\s*=\s*\[(.*?)\];', data, re.S)
        assert m, (profile, prop)
        raw = bytes.fromhex(re.sub(r'/\*.*?\*/|//[^\n]*', '', m[1], flags=re.S))
        sets.append(dict(property=prop, hex=raw.hex()))
        name = f'p{ident}_s{k}'
        arrays.append(f'static const unsigned char {name}[]={{'+','.join(map(str,raw))+'};')
        setups.append(f'if(id=={ident}) load(&p->lge.lge_cmd_sets[{k}],{name},sizeof({name}));')
    records.append(dict(id=ident, name=profile, path=path, sets=sets))

prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
typedef uint32_t u32;
#define pr_err(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_debug(...) ((void)0)
#define mutex_lock(x) ((void)0)
#define mutex_unlock(x) ((void)0)
#define DDIC_REG_DEBUG 0
#define CM_HUE_INDEX 6
#define CM_SATURATION_INDEX 24
#define SHARPNESS_OFF 0x32
#define RGB_OFFSET 3
#define BC_DIM_ON 1
#define BC_DIM_FRAMES_NORMAL 2
#define MIPI_DSI_MSG_USE_LPM 2
#define MIPI_DSI_MSG_LASTCOMMAND 8
enum {RED,GREEN,BLUE,RGB_ALL};
enum {screen_mode_auto=0,screen_mode_cinema,screen_mode_photos,screen_mode_web,
      screen_mode_sports,screen_mode_game,screen_mode_expert=10};
enum lge_ddic_dsi_cmd_set_type {LGE_DDIC_DSI_DISP_CM_COMMAND_DUMMY,
 LGE_DDIC_DSI_DISP_CM_SET,LGE_DDIC_DSI_RGB_LUT,LGE_DDIC_DSI_DISP_RGB_HUE_LUT,
 LGE_DDIC_DSI_DISP_SAT_LUT,LGE_DDIC_DSI_SET_SHARPNESS,LGE_DDIC_DSI_DISP_SHA_LUT};
enum dsi_cmd_set_state {DSI_CMD_SET_STATE_LP,DSI_CMD_SET_STATE_HS};
struct mipi_dsi_msg {unsigned char type,channel;unsigned flags;size_t tx_len;void*tx_buf;};
struct mipi_dsi_host;
struct mipi_dsi_host_ops {ssize_t(*transfer)(struct mipi_dsi_host*,const struct mipi_dsi_msg*);};
struct mipi_dsi_host {const struct mipi_dsi_host_ops*ops;};
struct dsi_cmd_desc {struct mipi_dsi_msg msg;bool last_command;unsigned post_wait_ms;};
struct cmd_set {struct dsi_cmd_desc*cmds;unsigned count;enum dsi_cmd_set_state state;
 struct dsi_cmd_desc storage[40];unsigned char payload[40][160];};
struct dsi_panel;
struct lge_ddic_ops {void(*lge_set_screen_mode)(struct dsi_panel*,bool);
 void(*lge_bc_dim_set)(struct dsi_panel*,int,int);void(*lge_set_tc_perf)(struct dsi_panel*,int);};
struct dsi_panel {const char*name;struct mipi_dsi_host*host;int panel_lock;bool initialized;
 struct {int cm_preset_step,cm_red_step,cm_green_step,cm_blue_step;
 int sc_hue_step,sc_sat_step,sc_sha_step,hdr_mode,screen_mode,tc_perf;
 bool use_ddic_reg_backup,ddic_reg_backup_complete,use_color_manager,use_tc_perf,is_sent_bc_dim_set;
 struct lge_ddic_ops*ddic_ops;struct cmd_set lge_cmd_sets[7];}lge;};
static int rgb_preset[5][3]={{12,0,12},{6,0,6},{0,0,0},{0,6,0},{0,12,0}};
struct r66456_iq_data {char mode[8];};
static struct r66456_iq_data r66456_color_space_value[8]={{"def"},{"cin"},{"pho"},{"web"},{"spo"},{"gam"},{"exp"},{"hdr"}};
static char*get_payload_addr(struct dsi_panel*p,enum lge_ddic_dsi_cmd_set_type t,int i){
 assert(i>=0&&(unsigned)i<p->lge.lge_cmd_sets[t].count);return p->lge.lge_cmd_sets[t].cmds[i].msg.tx_buf;}
static int get_payload_cnt(struct dsi_panel*p,enum lge_ddic_dsi_cmd_set_type t,int i){
 assert(i>=0&&(unsigned)i<p->lge.lge_cmd_sets[t].count);return p->lge.lge_cmd_sets[t].cmds[i].msg.tx_len;}
static bool dsi_panel_initialized(struct dsi_panel*p){return p->initialized;}
static void dump_cm_dummy_reg_value(struct dsi_panel*p){(void)p;assert(0);}
static unsigned np, fail_at, attempts;
static struct {unsigned type,flags,delay,n;unsigned char payload[160];}wire[6];
static ssize_t transfer(struct mipi_dsi_host*h,const struct mipi_dsi_msg*m){
 (void)h;++attempts;if(fail_at==attempts)return -EIO;
 assert(np<6&&m->tx_len<=160&&m->channel==0);wire[np].type=m->type;wire[np].flags=m->flags;
 wire[np].n=m->tx_len;memcpy(wire[np].payload,m->tx_buf,m->tx_len);++np;return m->tx_len;}
static void usleep_range(unsigned lo,unsigned hi){assert(np&&hi==lo+10);wire[np-1].delay=lo/1000;}
'''
# Use the actual supporting constants/tables too, so a different enum,
# preset or mode map cannot silently turn the unchanged helpers into a model.
ops_data=read(ops)
cm_header=read(lg+'cm/lge_color_manager.h')
for macro in ['DDIC_REG_DEBUG','CM_HUE_INDEX','CM_SATURATION_INDEX','SHARPNESS_OFF','RGB_OFFSET']:
 actual=re.search(r'^#define '+macro+r'\s+[^\n]+',ops_data,re.M)[0]
 prelude=re.sub(r'^#define '+macro+r'\s+[^\n]+',lambda m:actual,prelude,flags=re.M)
for macro in ['PRESET_SETP0_INDEX','PRESET_SETP1_INDEX','PRESET_SETP2_INDEX','STEP_DG_PRESET']:
 prelude=re.search(r'^#define '+macro+r'\s+[^\n]+',ops_data,re.M)[0]+'\n'+prelude
actual_rgb=re.search(r'static int rgb_preset\[.*?\n};',ops_data,re.S)[0]
prelude=re.sub(r'static int rgb_preset[^;]+;',lambda m:actual_rgb,prelude)
actual_modes=re.search(r'static struct r66456_iq_data r66456_color_space_value\[.*?\n};',ops_data,re.S)[0]
prelude=re.sub(r'static struct r66456_iq_data r66456_color_space_value[^;]+;',lambda m:actual_modes,prelude)
actual_enum=re.search(r'enum \{\s*screen_mode_auto.*?};',cm_header,re.S)[0]
prelude=re.sub(r'enum \{screen_mode_auto.*?};',lambda m:actual_enum,prelude,flags=re.S)
body = extract(lg+'lge_ddic_ops.c','lge_ddic_dsi_panel_tx_cmd_set')
for name in ['update_color_table','color_space_mapper','custom_rgb_mapper','screen_tune_mapper',
             'screen_tune_sharpness','lge_display_control_store_r66456','lge_set_screen_mode_r66456']:
    body += extract(ops,name)
body += extract(lg+'lge_dsi_panel.c','lge_dsi_panel_mode_set')
tail = r'''
static void load(struct cmd_set*s,const unsigned char*b,size_t n){
 s->cmds=s->storage;s->state=DSI_CMD_SET_STATE_HS;
 while(n){assert(n>=7&&s->count<40);unsigned len=b[5]*256u+b[6],i=s->count++;
 assert(n>=7+len&&len<=160&&b[3]==0);struct dsi_cmd_desc*c=&s->storage[i];
 c->msg=(struct mipi_dsi_msg){b[0],b[2],0,len,s->payload[i]};c->last_command=b[1];c->post_wait_ms=b[4];
 memcpy(s->payload[i],b+7,len);b+=7+len;n-=7+len;}
}
static void setup(struct dsi_panel*p,unsigned id){
 static const struct mipi_dsi_host_ops hostops={transfer};static struct mipi_dsi_host host={&hostops};
 static struct lge_ddic_ops ops={lge_set_screen_mode_r66456,0,0};
 memset(p,0,sizeof(*p));p->name="r66456";p->host=&host;p->initialized=true;
 p->lge.use_color_manager=true;p->lge.cm_preset_step=2;p->lge.ddic_ops=&ops;
 np=attempts=fail_at=0;memset(wire,0,sizeof(wire));
 SETUPS
}
static void emit(unsigned id){
 printf("%u ",id);for(unsigned i=0;i<np;++i){assert(wire[i].flags==8);
 printf("%02x010000%02x%04x",wire[i].type,wire[i].delay,wire[i].n);
 for(unsigned j=0;j<wire[i].n;++j)printf("%02x",wire[i].payload[j]);}puts("");
}
int main(void){
 for(unsigned id=1;id<=4;++id){struct dsi_panel p;setup(&p,id);
 assert(lge_dsi_panel_mode_set(&p)==0&&np==6);emit(id);
 assert(wire[0].payload[0]==0xb0&&wire[1].payload[0]==0xce&&wire[1].delay==(id==4?20u:0u));
 assert(wire[4].payload[0]==0xdd&&wire[4].payload[1]==0x32);
 unsigned char normal[160];memcpy(normal,wire[1].payload,wire[1].n);
 setup(&p,id);p.lge.use_color_manager=false;assert(lge_dsi_panel_mode_set(&p)==0&&np==0);
 setup(&p,id);p.lge.use_ddic_reg_backup=true;assert(lge_dsi_panel_mode_set(&p)==0&&np==0);
 setup(&p,id);p.lge.ddic_reg_backup_complete=true;assert(lge_dsi_panel_mode_set(&p)==0&&np==0);
 setup(&p,id);p.lge.use_ddic_reg_backup=p.lge.ddic_reg_backup_complete=true;
 assert(lge_dsi_panel_mode_set(&p)==0&&np==6);
 setup(&p,id);p.lge.hdr_mode=1;assert(lge_dsi_panel_mode_set(&p)==0&&np==6);
 assert(memcmp(normal,wire[1].payload,wire[1].n));
 setup(&p,id);p.lge.screen_mode=screen_mode_expert;p.lge.sc_sha_step=4;
 assert(lge_dsi_panel_mode_set(&p)==0&&np==6&&wire[4].payload[1]==0x33);
 setup(&p,id);p.lge.cm_red_step=1;assert(lge_dsi_panel_mode_set(&p)==0&&np==6);
 assert(wire[1].payload[3]!=normal[3]);
 // The void LG mapper ignores the first set's error and still sends sharpness.
 // XNU deliberately stops its entire six-packet restore on any uncertain exit.
 setup(&p,id);fail_at=1;assert(lge_dsi_panel_mode_set(&p)==0&&attempts==4&&np==3);
 }
 return 0;
}
'''.replace('SETUPS','\n'.join(setups))
source = out/'color-reference.c'
source.write_text(prelude+'\n'.join(arrays)+'\n'+body+tail)
binary = out/'color-reference'
subprocess.run([os.environ.get('CC','clang-19'),'-std=c11','-g','-fsanitize=address,undefined',str(source),'-o',str(binary)],check=True)
result = subprocess.check_output([str(binary)],text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
for line, rec in zip(result.splitlines(),records):
    ident, wire = line.split()
    assert int(ident)==rec['id']
    rec['sdr_auto_restore_hex']=wire
assert len(result.splitlines())==4
record = dict(pin=pin,functions=functions,sources=sources,profiles=records,
              policy=dict(screen_mode=0,hdr_mode=0,cm_preset_step=2,cm_red_step=0,cm_green_step=0,cm_blue_step=0,sharpness='off'),
              boundary='ordinary SDR Auto defaults only; not Android userspace policy, HDR, dynamic tuning or complete DRM',
              error_difference='LG void mapper ignores set failure; XNU must stop before pixels on any failure')
(out/'color-reference.json').write_text(json.dumps(record,indent=2)+'\n')
fixture=Path(__file__).resolve().parent/'fixtures/mh2lm-panel-sdr-color.json'
if fixture.exists():
 expected=json.loads(fixture.read_text())
 assert expected['pin']==pin and expected['policy']==record['policy']
 assert [p['sdr_auto_restore_hex'] for p in expected['profiles']]==[p['sdr_auto_restore_hex'] for p in records]

print('PASS: nine unchanged LG functions; four profiles, six HS packets, PV-only 20ms CM delay, default RGB and sharpness32; color-manager/backup guards and distinct HDR/expert/RGB branches; LG ignored-error behavior recorded')

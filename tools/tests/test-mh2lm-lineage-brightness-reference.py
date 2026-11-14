#!/usr/bin/env python3
"""Execute pinned LG pending-brightness, post-kickoff and MIPI helpers unchanged.
Usage: test-mh2lm-lineage-brightness-reference.py LINEAGE_ROOT [EVIDENCE_DIR]
Real msm complete_commit is executed; its DRM/KMS leaves are stubs. Intermediate
KMS/connector bindings, initialization and DTS guards are audited statically.
This is not a complete DRM, clock-manager, backlight-map or hardware execution.
"""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,tempfile
if len(sys.argv) not in (2,3):sys.exit(__doc__)
root=Path(sys.argv[1]).resolve();pin='2e897dddf49a4c3e85b2ff77fdf7c70f860398dd'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()==pin
sources={};functions=[]
def read(path):
 data=(root/path).read_text();assert subprocess.check_output(['git','show',f'{pin}:{path}'],cwd=root)==data.encode()
 sources[path]=hashlib.sha256(data.encode()).hexdigest();return data
def extract(path,name,execute=True):
 data=read(path);m=re.search(r'^(?:static )?(?:int|void|ssize_t)\s+'+name+r'\s*\([^;{}]*\)\s*\{',data,re.M);assert m,name
 masked=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),data,flags=re.S)
 pos=m.end();depth=1
 while depth:depth+=(masked[pos]=='{')-(masked[pos]=='}');pos+=1
 if execute:functions.append(name)
 return data[m.start():pos]+'\n'
msm='drivers/gpu/drm/msm/';lg=msm+'lge/brightness/lge_brightness.c';dsi=msm+'dsi-staging/dsi_display.c';mipi='drivers/gpu/drm/drm_mipi_dsi.c'
checks=[]
def ordered(text,*needles):
 last=-1
 for n in needles:pos=text.find(n);assert pos>last,n;last=pos
 checks.append(list(needles))
kms=extract(msm+'sde/sde_kms.c','sde_kms_complete_commit',False)
ordered(kms,'sde_kms_power_resource_is_enabled','c_conn->ops.post_kickoff(connector, &params)','sde_power_resource_enable')
assert '.complete_commit = sde_kms_complete_commit' in read(msm+'sde/sde_kms.c')
assert '.post_kickoff = dsi_conn_post_kickoff' in read(msm+'sde/sde_kms.c')
bridge=extract(msm+'dsi-staging/dsi_drm.c','dsi_conn_post_kickoff',False)
assert 'dsi_display_post_kickoff(display)' in bridge
wait=extract(msm+'msm_atomic.c','msm_atomic_wait_for_commit_done',False)
assert 'wait_for_crtc_commit_done' in wait
panel=read(msm+'dsi-staging/dsi_panel.c')
assert 'panel = kzalloc(sizeof(*panel), GFP_KERNEL)' in panel
assert not re.search(r'(?:mipi_device|dsi)->?\.?mode_flags\s*[|&]?=',panel)
# The only generic mode writes are in other DRM hosts/panel drivers, not this
# pinned MSM/LG device path. Exclude comparisons when checking assignments.
for directory in (root/msm/'dsi-staging',root/msm/'lge'):
 for path in directory.rglob('*.c'):
  data=read(str(path.relative_to(root)))
  assert not re.search(r'(?:->|\.)mode_flags\s*(?:=(?!=)|[|&^]=)',data),path
checks.append(['MSM/LG mode_flags assignment absent; target device zero-initialized'])
header=read('include/drm/drm_mipi_dsi.h')
for name,bit in [('MIPI_DSI_MSG_USE_LPM',1),('MIPI_DSI_MSG_LASTCOMMAND',3),('MIPI_DSI_MODE_LPM',11)]:
 assert re.search(r'#define\s+'+name+r'\s+BIT\('+str(bit)+r'\)',header)
common=read('arch/arm64/boot/dts/lge/sm8150_common/sm8150_common-panel.dtsi')
for profile in ('pv','pp1','dv1','dv2'):
 body=re.search(r'&dsi_r66456_tianmamd_'+profile+r'_fhd_cmd\s*\{([^}]+)',common).group(1)
 assert 'lge,dcs-brightness-bigendian;' in body
 data=read('arch/arm64/boot/dts/lge/sm8150_common/settings_panel/dsi-panel-r66456-'+profile+'-tianmamd-fhd-cmd.dtsi')
 for excluded in ('lge,use-dim-ctrl;','lge,dcs-brightness-short;','lge,disp-br-ctrl-ext-supported;','qcom,bl-update-flag'):
  assert excluded not in data+body,excluded
 for flag in ('lge,use-irc-ctrl;','lge,use-ace-ctrl;'):assert flag in data
 for which in ('on','off'):assert f'qcom,mdss-dsi-{which}-command-state = "dsi_lp_mode";' in data
checks.append(['four target DTS: big-endian long brightness, ON/OFF LP, dim disabled, IRC/ACE enabled but require pending state'])
prelude=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define IS_ENABLED(x) 0
#define pr_err(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_debug(...) ((void)0)
#define SDE_ERROR(...) ((void)0)
#define mutex_lock(x) ((void)0)
#define mutex_unlock(x) ((void)0)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define GFP_KERNEL 0
#define kmalloc(n,f) malloc(n)
#define kfree(p) free(p)
#define MIPI_DSI_MODE_LPM (1u<<11)
#define MIPI_DSI_MSG_USE_LPM (1u<<1)
#define MIPI_DSI_MSG_LASTCOMMAND (1u<<3)
#define MIPI_DSI_DCS_SHORT_WRITE 5
#define MIPI_DSI_DCS_SHORT_WRITE_PARAM 21
#define MIPI_DSI_DCS_LONG_WRITE 57
#define MIPI_DCS_SET_DISPLAY_BRIGHTNESS 0x51
#define FB_BLANK_UNBLANK 0
#define BL_CORE_FBBLANK 1
#define BL_CORE_SUSPENDED 2
#define DRM_EVENT_SYS_BACKLIGHT 1
#define LGE_PANEL_LP1 1
#define LGE_PANEL_LP2 2
#define SDE_MODE_DPMS_ON 0
#define SDE_MODE_DPMS_LP1 1
#define SDE_MODE_DPMS_LP2 2
#define BC_DIM_OFF 0
#define LGE_GLOBAL_IRC_HBM 0
struct mipi_dsi_msg {unsigned channel,type,flags;const void*tx_buf;size_t tx_len;};
struct mipi_dsi_host;
struct mipi_dsi_host_ops {ssize_t(*transfer)(struct mipi_dsi_host*,const struct mipi_dsi_msg*);};
struct mipi_dsi_host {const struct mipi_dsi_host_ops*ops;};
struct mipi_dsi_device {struct mipi_dsi_host*host;unsigned channel,mode_flags;};
struct drm_connector {int base;void*dev;};
struct dsi_panel;
struct ddic_ops {int(*set_dim_ctrl)(struct dsi_panel*,bool);int(*set_irc_state)(struct dsi_panel*,int,int);};
struct backlight_device {int ops_lock;void*data;struct {int brightness,power,state;}props;};
struct dsi_panel {struct mipi_dsi_host*host;struct mipi_dsi_device mipi_device;
 struct {struct backlight_device*raw_bd;unsigned bl_max_level,brightness_max_level;}bl_config;
 struct {bool allow_bl_update,allow_bl_update_ex,panel_dead,vr_lp_mode,hdr_mode,use_color_manager,video_enhancement,use_dynamic_brightness,brightness_table;
 bool use_dim_ctrl,use_irc_ctrl,use_ace_ctrl,irc_pending,use_br_ctrl_ext,use_dcs_brightness_short,dcs_brightness_be;
 int bl_lvl_unset,bl_ex_lvl_unset,lp_state,bc_dim_en,irc_request_state;struct ddic_ops*ddic_ops;}lge;};
struct dsi_display {struct mipi_dsi_host host;struct dsi_panel*panel;struct drm_connector*drm_conn;int display_lock;};
struct sde_connector {struct drm_connector base;void*display;bool panel_dead;int last_panel_power_mode;
 struct {int(*set_backlight)(struct drm_connector*,void*,u32);}ops;};
#define to_sde_connector(p) ((p)?container_of(p,struct sde_connector,base):NULL)
#define bl_get_data(p) ((p)->data)
struct drm_event {int type,length;};
enum lge_blmap_type {LGE_BLMAP_DEFAULT,LGE_BLMAP_EX,LGE_BLMAP_VR,LGE_BLMAP_HDR,LGE_BLMAP_VE,LGE_BLMAP_BRIGHTER};
struct lge_blmap {int size;int*map;};
static struct lge_blmap*lge_get_blmap(struct dsi_panel*p,enum lge_blmap_type t){return NULL;}
#define mult_frac(x,n,d) ((x)*(n)/(d))
static void msm_mode_object_event_notify(void*a,void*b,void*c,void*d){}
static bool interactive=true;static unsigned sends,dimCalls,ircCalls;static int sendError;
static unsigned lastFlags,lastType;static u8 lastPayload[3];
static char events[4096];
static void event(const char*s){strcat(events,s);strcat(events," ");}
static ssize_t transfer(struct mipi_dsi_host*h,const struct mipi_dsi_msg*m){
 ++sends;lastFlags=m->flags;lastType=m->type;assert(m->tx_len==3);memcpy(lastPayload,m->tx_buf,3);event("wire");return sendError?sendError:3;
}
static bool lge_dsi_panel_is_power_on_interactive(struct dsi_panel*p){return interactive;}
static int dsi_panel_update_backlight(struct dsi_panel*,u32);
static int dsi_display_set_backlight(struct drm_connector*c,void*d,u32 level){return dsi_panel_update_backlight(((struct dsi_display*)d)->panel,level);}
static int lge_update_backlight_ex(struct dsi_panel*p){assert(0);return 0;}
static int lge_mipi_dsi_dcs_set_display_brightness_extension_short(struct dsi_panel*p,u32 l){assert(0);return 0;}
static int mipi_dsi_dcs_set_display_brightness_short(struct mipi_dsi_device*d,u32 l){assert(0);return 0;}
static int dim(struct dsi_panel*p,bool b){++dimCalls;return 0;}
static int irc(struct dsi_panel*p,int a,int b){++ircCalls;return 0;}
'''
code=''.join(extract(mipi,n) for n in ('mipi_dsi_device_transfer','mipi_dsi_dcs_write_buffer','mipi_dsi_dcs_write','mipi_dsi_dcs_set_display_brightness'))
code+=''.join(extract(lg,n) for n in ('dsi_panel_update_backlight','lge_update_backlight','lge_backlight_device_update_status'))
code+=extract(dsi,'dsi_display_post_kickoff')
upper=r'''
struct drm_atomic_state;struct msm_kms;
struct kms_funcs {void(*prepare_commit)(struct msm_kms*,struct drm_atomic_state*);void(*complete_commit)(struct msm_kms*,struct drm_atomic_state*);};
struct msm_kms {struct kms_funcs*funcs;};
struct msm_drm_private {struct msm_kms*kms;};
struct drm_device {void*dev_private;};
struct drm_atomic_state {struct drm_device*dev;};
struct msm_commit {struct drm_atomic_state*state;};
static struct dsi_display*active;
static bool waited;
static void drm_atomic_helper_wait_for_fences(struct drm_device*d,struct drm_atomic_state*s,bool b){event("fences");}
static void prepare(struct msm_kms*k,struct drm_atomic_state*s){event("prepare");}
static void msm_atomic_helper_commit_modeset_disables(struct drm_device*d,struct drm_atomic_state*s){event("disable");}
static void drm_atomic_helper_commit_planes(struct drm_device*d,struct drm_atomic_state*s,int i){event("planes");}
static void msm_atomic_helper_commit_modeset_enables(struct drm_device*d,struct drm_atomic_state*s){event("enable");}
static void msm_atomic_wait_for_commit_done(struct drm_device*d,struct drm_atomic_state*s){event("wait");assert(!sends);waited=true;}
static void drm_atomic_helper_cleanup_planes(struct drm_device*d,struct drm_atomic_state*s){event("cleanup");}
/* KMS/connector bodies are statically audited above; this leaf represents
 * their ordinary ON callback and does not simulate DRM power/IRQ completion. */
static void kmsComplete(struct msm_kms*k,struct drm_atomic_state*s){assert(waited);event("post");assert(!dsi_display_post_kickoff(active));}
static void drm_atomic_state_put(struct drm_atomic_state*s){event("put");}
static void commit_destroy(struct msm_commit*c){event("destroy");}
'''+extract(msm+'msm_atomic.c','complete_commit')
test=r'''
int main(void){
 struct mipi_dsi_host_ops ops={.transfer=transfer};struct dsi_display d={.host={.ops=&ops}};
 struct dsi_panel p={.host=&d.host,.mipi_device={.host=&d.host},.bl_config={.bl_max_level=4095,.brightness_max_level=4095}};
 struct sde_connector c={.display=&d,.ops={.set_backlight=dsi_display_set_backlight}};
 struct backlight_device bd={.data=&c};d.panel=&p;d.drm_conn=&c.base;p.bl_config.raw_bd=&bd;
 struct ddic_ops ddic={dim,irc};p.lge.ddic_ops=&ddic;p.lge.dcs_brightness_be=true;p.lge.use_irc_ctrl=p.lge.use_ace_ctrl=true;
 struct kms_funcs funcs={prepare,kmsComplete};struct msm_kms kms={&funcs};struct msm_drm_private priv={&kms};struct drm_device dev={&priv};
 struct drm_atomic_state state={&dev};struct msm_commit commit={&state};active=&d;
 // Actual update_status overwrites the pending level without sending while
 // disallowed. No mapping table in this fixture: direct brightness levels.
 bd.props.brightness=0x345;assert(!lge_backlight_device_update_status(&bd)&&!sends&&p.lge.bl_lvl_unset==0x345);
 bd.props.brightness=0x678;assert(!lge_backlight_device_update_status(&bd)&&!sends&&p.lge.bl_lvl_unset==0x678);
 complete_commit(&commit);
 assert(!strcmp(events,"fences prepare disable planes enable wait cleanup post wire put destroy "));
 assert(sends==1&&p.lge.allow_bl_update&&p.lge.bl_lvl_unset==-1&&!dimCalls&&!ircCalls);
 assert(lastType==57&&lastFlags==MIPI_DSI_MSG_LASTCOMMAND&&!memcmp(lastPayload,"\x51\x06\x78",3));
 assert(!dsi_display_post_kickoff(&d)&&sends==1); // already allowed
 bd.props.brightness=0x789;assert(!lge_backlight_device_update_status(&bd)&&sends==2&&!memcmp(lastPayload,"\x51\x07\x89",3));
 p.mipi_device.mode_flags=MIPI_DSI_MODE_LPM;assert(!dsi_panel_update_backlight(&p,0x234));assert(lastFlags==(MIPI_DSI_MSG_LASTCOMMAND|MIPI_DSI_MSG_USE_LPM));p.mipi_device.mode_flags=0;
 unsigned before=sends;p.lge.allow_bl_update=false;p.lge.bl_lvl_unset=-1;assert(!dsi_display_post_kickoff(&d)&&sends==before&&!p.lge.allow_bl_update);
 p.lge.bl_lvl_unset=0x345;interactive=false;assert(!dsi_display_post_kickoff(&d)&&sends==before&&!p.lge.allow_bl_update);interactive=true;
 c.last_panel_power_mode=3;assert(!dsi_display_post_kickoff(&d)&&sends==before);c.last_panel_power_mode=0;
 assert(lge_update_backlight(NULL)==-EINVAL);p.bl_config.raw_bd=NULL;assert(lge_update_backlight(&p)==-EINVAL);p.bl_config.raw_bd=&bd;
 // Record the native error semantics explicitly: it clears pending even on
 // failure. XNU intentionally keeps stricter failure/uncertain protection.
 sendError=-EIO;assert(dsi_display_post_kickoff(&d)==-EIO&&p.lge.allow_bl_update&&p.lge.bl_lvl_unset==-1);
 sendError=0;p.lge.irc_pending=true;assert(!dsi_display_post_kickoff(&d)&&ircCalls==1&&!dimCalls);
 puts("PASS: nine unchanged LG/MSM/MIPI functions; real complete_commit order, pending latest target, post-kickoff guards, long 51 high low, default HS/explicit LP, native error semantics, IRC pending guard; intermediate KMS/connector and target DTS statically audited");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-brightness-reference-') as tmp:
 tmp=Path(tmp);c=tmp/'reference.c';c.write_text(prelude+code+upper+test);binary=tmp/'reference'
 subprocess.run([os.environ.get('CC','clang-19'),'-std=c11','-g','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
 result=subprocess.check_output([str(binary)],text=True,env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0",UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"));print(result,end='')
 if len(sys.argv)==3:
  out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True);(out/'brightness-reference.c').write_text(c.read_text())
  (out/'brightness-reference.json').write_text(json.dumps(dict(pin=pin,sources=sources,functions=functions,static_checks=checks,scope=__doc__,result=result),indent=2)+'\n')

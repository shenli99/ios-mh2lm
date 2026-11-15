#!/usr/bin/env python3
"""Execute exact pinned LG DSI outer callbacks and SDE resource dispatcher.
Usage: test-mh2lm-display-client-clock-reference.py LINEAGE_ROOT [EVIDENCE_DIR]
Real clock manager/callbacks; SDE resource helper, OS, PHY/CCF are fixture leaves.
No complete DRM/CCF integration or hardware/power-collapse proof.
"""
from pathlib import Path
import json, os, subprocess, sys, tempfile
if len(sys.argv) not in (2,3): sys.exit(__doc__)
p = Path(__file__).with_name('test-mh2lm-lineage-clock-reference.py')
n = {'__file__':str(p), '__name__':'client_clock_reference'}
exec(compile(p.read_text().split('with tempfile.TemporaryDirectory(')[0],str(p),'exec'),n)
code = n['code'].split('static void expect(')[0]
code = code.replace('struct dsi_panel {bool', 'struct dsi_mode {unsigned dsi_mode_flags;};\nstruct dsi_panel {struct dsi_mode*cur_mode;bool')
code = code.replace('struct dsi_display {struct', 'struct dsi_display {void*dsi_clk_handle;bool poms_pending;struct {unsigned panel_mode;}config;struct')
code += r'''
#define DSI_MODE_FLAG_POMS 1u
#define DSI_OP_CMD_MODE 1u
#define DSI_OP_VIDEO_MODE 2u
static int dsi_display_clk_ctrl(void*h,enum dsi_clk_type t,enum dsi_clk_state s){event(s==DSI_CLK_ON?"dsi-on":"dsi-off");return dsi_clk_req_state(h,t,s);}
static int dsi_panel_post_enable(struct dsi_panel*p){event("panel-post-enable");return 0;}
static int dsi_panel_pre_disable(struct dsi_panel*p){event("panel-pre-disable");return 0;}
static int dsi_panel_mode_switch_to_cmd(struct dsi_panel*p){assert(false);return 0;}
static int dsi_panel_mode_switch_to_vid(struct dsi_panel*p){assert(false);return 0;}
static int dsi_panel_pre_mode_switch_to_video(struct dsi_panel*p){assert(false);return 0;}
static int dsi_panel_pre_mode_switch_to_cmd(struct dsi_panel*p){assert(false);return 0;}
'''
code += n['extract'](n['display'],'dsi_display_post_enable')
code += n['extract'](n['display'],'dsi_display_pre_disable')
code += r'''
#define SDE_ERROR(...) ((void)0)
#define SDE_ERROR_ENC(...) ((void)0)
#define SDE_DEBUG(...) ((void)0)
#define SDE_DEBUG_ENC(...) ((void)0)
#define SDE_EVT32(...) ((void)0)
#define SDE_EVT32_VERBOSE(...) ((void)0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MSM_DISPLAY_VIDEO_MODE 2
#define MSM_ENC_TX_COMPLETE 1
#define DRM_MODE_CONNECTOR_DSI 1
#define SDE_MODE_DPMS_ON 0
#define SDE_MODE_DPMS_LP2 2
#define IDLE_SHORT_TIMEOUT 1
#define IDLE_POWERCOLLAPSE_DURATION 58
#define IDLE_POWERCOLLAPSE_IN_EARLY_WAKEUP 80
#define msecs_to_jiffies(n) (n)
enum {SDE_ENC_RC_EVENT_KICKOFF,SDE_ENC_RC_EVENT_FRAME_DONE,SDE_ENC_RC_EVENT_PRE_STOP,SDE_ENC_RC_EVENT_STOP,SDE_ENC_RC_EVENT_PRE_MODESET,SDE_ENC_RC_EVENT_POST_MODESET,SDE_ENC_RC_EVENT_ENTER_IDLE,SDE_ENC_RC_EVENT_EARLY_WAKEUP};
enum {SDE_ENC_RC_STATE_OFF,SDE_ENC_RC_STATE_ON,SDE_ENC_RC_STATE_IDLE,SDE_ENC_RC_STATE_PRE_OFF,SDE_ENC_RC_STATE_MODESET};
struct msm_drm_thread {int worker;};
struct msm_drm_private {struct msm_drm_thread disp_thread[1];};
struct drm_device {struct msm_drm_private*dev_private;};
struct drm_encoder {struct drm_device*dev;};
struct fixture_crtc {unsigned index,pending;};
struct sde_encoder_phys {struct {bool(*is_autorefresh_enabled)(struct sde_encoder_phys*);}ops;void*connector;};
struct sde_encoder_virt {struct drm_encoder base;struct fixture_crtc*crtc;struct sde_encoder_phys*cur_master;bool idle_pc_enabled;unsigned rc_state;int rc_lock,delayed_off_work,vsync_event_work,vsync_event_timer;struct {int intf_type;}disp_info;unsigned frame_busy_mask[1];};
#define to_sde_encoder_virt(p) container_of(p,struct sde_encoder_virt,base)
static bool autorefresh=true;
static unsigned schedules=0,acquires=0,releases=0;
static struct dsi_clk_client_info*mdp_client;
static bool sde_encoder_check_curr_mode(struct drm_encoder*e,int mode){return false;}
static bool kthread_cancel_delayed_work_sync(void*p){return false;}
static void kthread_cancel_work_sync(void*p){}
static void del_timer_sync(void*p){}
static void msm_idle_set_state(struct drm_encoder*e,bool on){}
static void _sde_encoder_irq_control(struct drm_encoder*e,bool on){}
static int _sde_encoder_resource_control_helper(struct drm_encoder*e,bool on){
 if(on)++acquires;else ++releases;
 event(on?"mdp-on":"mdp-off");return dsi_clk_req_state(mdp_client,DSI_ALL_CLKS,on?DSI_CLK_ON:DSI_CLK_OFF);
}
static void _sde_encoder_resource_control_rsc_update(struct drm_encoder*e,bool on){}
static unsigned sde_crtc_frame_pending(struct fixture_crtc*c){return c->pending;}
static unsigned sde_connector_get_lp(void*c){return SDE_MODE_DPMS_ON;}
static void kthread_mod_delayed_work(void*w,void*d,unsigned ms){assert(ms==IDLE_POWERCOLLAPSE_DURATION||ms==IDLE_POWERCOLLAPSE_IN_EARLY_WAKEUP);++schedules;}
static int sde_encoder_wait_for_event(struct drm_encoder*e,unsigned event){return 0;}
static void _sde_encoder_modeset_helper_locked(struct drm_encoder*e,unsigned event){}
static void _sde_encoder_update_rsc_client(struct drm_encoder*e,void*p,bool on){}
static bool refresh_enabled(struct sde_encoder_phys*p){return autorefresh;}
'''
code += n['extract']('drivers/gpu/drm/msm/sde/sde_encoder.c','sde_encoder_resource_control')
code += 'static void expect(' + n['code'].split('static void expect(',1)[1].split(' // Ordinary blank:',1)[0]
code += r'''
 struct dsi_mode mode={0};panel.cur_mode=&mode;display.config.panel_mode=DSI_OP_CMD_MODE;display.dsi_clk_handle=&dsi;
 struct msm_drm_private priv={0};struct drm_device dev={&priv};struct fixture_crtc crtc={0};
 struct sde_encoder_phys phys={.ops={refresh_enabled}};
 struct sde_encoder_virt enc={.base={&dev},.crtc=&crtc,.cur_master=&phys,.idle_pc_enabled=true,.disp_info={DRM_MODE_CONNECTOR_DSI}};
 mdp_client=&mdp;
 // DSI prepare's starting reference is constructed above. Actual KICKOFF
 // takes MDP; actual panel post_enable releases DSI without physical OFF.
 assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_KICKOFF));expect("mdp-on ");
 assert(mdp.core_refcount==1&&mdp.link_refcount==1);
 assert(!dsi_display_post_enable(&display));expect("panel-post-enable dsi-off ");
 assert(!dsi.core_refcount&&!dsi.link_refcount&&m.link_clk_state==DSI_CLK_ON);
 for(unsigned frame=0;frame<20;++frame){
  assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_KICKOFF));
  assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_FRAME_DONE));
  assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_EARLY_WAKEUP));
 }
 assert(!events[0]&&!schedules&&acquires==1&&!releases);
 // Ordinary non-autorefresh uses delayed idle; pending>1 suppresses it.
 autorefresh=false;crtc.pending=2;
 assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_FRAME_DONE)&&!schedules);
 crtc.pending=1;assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_FRAME_DONE)&&schedules==1);
 autorefresh=true;
 // Bridge pre_disable takes DSI BEFORE encoder PRE_STOP/STOP drops MDP.
 assert(!dsi_display_pre_disable(&display));expect("dsi-on panel-pre-disable ");
 assert(sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_STOP)==-EINVAL&&!releases);
 assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_PRE_STOP));
 assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_STOP));expect("mdp-off ");
 assert(dsi.core_refcount==1&&dsi.link_refcount==1&&!mdp.core_refcount&&!mdp.link_refcount&&m.link_clk_state==DSI_CLK_ON);
 assert(!sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_STOP)&&releases==1&&!events[0]);
 // Invalid FRAME_DONE does not enqueue idle after stop. Null callbacks reject.
 assert(sde_encoder_resource_control(&enc.base,SDE_ENC_RC_EVENT_FRAME_DONE)==-EINVAL&&schedules==1);
 assert(dsi_display_post_enable(NULL)==-EINVAL&&dsi_display_pre_disable(NULL)==-EINVAL);
 puts("PASS: 19 exact LG functions compiled; actual post_enable/pre_disable and SDE KICKOFF/FRAME_DONE/EARLY_WAKEUP/PRE_STOP/STOP; shared DSI/MDP refs, autorefresh suppresses idle, pending-frame guard, repeated kickoff/stop and invalid STOP ordering. Prepare/unprepare, full SDE helper, OS and physical leaves are outside this execution scope.");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-client-clock-reference-') as tmp:
    tmp=Path(tmp);(tmp/'reference.c').write_text(code)
    subprocess.run([os.environ.get('CC','clang-19'),'-std=c11','-g','-fsanitize=address,undefined',str(tmp/'reference.c'),'-o',str(tmp/'reference')],check=True)
    output=subprocess.check_output([str(tmp/'reference')],text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    print(output,end='')
    if len(sys.argv)==3:
        dest=Path(sys.argv[2]);dest.mkdir(parents=True,exist_ok=True)
        (dest/'display-client-clock-reference.c').write_text(code)
        (dest/'display-client-clock-reference.json').write_text(json.dumps(dict(pin=n['pin'],sources=n['sources'],functions=n['extracted'],static_audit=n['static_audit'],scope=__doc__,output=output),indent=2)+'\n')

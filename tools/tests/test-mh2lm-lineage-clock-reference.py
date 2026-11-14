#!/usr/bin/env python3
"""Execute pinned LG clock manager, callbacks and HS helpers unmodified.
Usage: test-mh2lm-lineage-clock-reference.py LINEAGE_ROOT [EVIDENCE_DIR]
OS/CCF, core/LP clock and PHY leaves are stubs. This proves caller ordering and
aggregate votes; it does not execute DRM, CCF topology, PLL MMIO or hardware.
"""
from pathlib import Path
import hashlib, json, os, re, subprocess, sys, tempfile
from mh2lm_lineage_clock_audit import audit

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
root = Path(sys.argv[1]).resolve()
pin = '2e897dddf49a4c3e85b2ff77fdf7c70f860398dd'
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip() == pin
static_audit = audit(root, pin)
sources, extracted = {}, []
def extract(path, name):
    data = (root / path).read_text()
    # Reject working-tree changes in the reference, not just a matching HEAD.
    assert subprocess.check_output(['git', 'show', f'{pin}:{path}'], cwd=root) == data.encode()
    sources[path] = hashlib.sha256(data.encode()).hexdigest()
    match = re.search(r'^(?:static )?(?:int|void)\s+' + name + r'\s*\([^;{}]*\)\s*\{', data, re.M)
    assert match, name
    pos, depth = match.end(), 1
    # Mask comments/strings while counting braces; retain the exact source.
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m[0]), data, flags=re.S)
    while depth:
        depth += (masked[pos] == '{') - (masked[pos] == '}')
        pos += 1
    extracted.append(name)
    return data[match.start():pos] + '\n'

prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define pr_warn(...) ((void)0)
#define MAX_DSI_CTRL 2
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define mutex_lock(p) ((void)0)
#define mutex_unlock(p) ((void)0)
enum dsi_clk_type { DSI_CORE_CLK=1, DSI_LINK_CLK=2, DSI_ALL_CLKS=3 };
enum dsi_lclk_type { DSI_LINK_NONE=0, DSI_LINK_LP_CLK=1, DSI_LINK_HS_CLK=2 };
enum dsi_clk_state { DSI_CLK_OFF, DSI_CLK_ON, DSI_CLK_EARLY_GATE };
enum dsi_link_clk_op_type { DSI_LINK_CLK_SET_RATE=1, DSI_LINK_CLK_PREPARE=2, DSI_LINK_CLK_ENABLE=4, DSI_LINK_CLK_START=7 };
static char events[16384];
static void event(const char*s){assert(strlen(events)+strlen(s)+2<sizeof(events));strcat(events,s);strcat(events," ");}
struct clk {const char *prepare,*enable,*disable,*unprepare;};
static int clk_prepare(struct clk*c){event(c->prepare);return 0;}
static int clk_enable(struct clk*c){event(c->enable);return 0;}
static void clk_disable(struct clk*c){event(c->disable);}
static void clk_unprepare(struct clk*c){event(c->unprepare);}
struct dsi_link_hs_clk_info {struct clk *byte_clk,*pixel_clk,*byte_intf_clk;};
struct dsi_link_lp_clk_info {int unused;};
struct dsi_link_clks {struct dsi_link_hs_clk_info hs_clks;struct dsi_link_lp_clk_info lp_clks;};
struct dsi_core_clks {int unused;};
struct list_head {struct list_head *next;};
#define list_for_each(p,h) for(p=(h)->next;p!=(h);p=p->next)
#define list_entry(p,t,m) container_of(p,t,m)
typedef int (*callback)(void*,enum dsi_clk_type,enum dsi_lclk_type,enum dsi_clk_state);
struct dsi_clk_mngr {
 void *priv_data;callback pre_clkon_cb,post_clkon_cb,pre_clkoff_cb,post_clkoff_cb;
 struct dsi_core_clks *core_clks;struct dsi_link_clks *link_clks;
 u32 core_clk_state,link_clk_state,dsi_ctrl_count,master_ndx;
 struct list_head client_list;int clk_mutex;
};
struct dsi_clk_client_info {struct dsi_clk_mngr*mngr;struct list_head list;u32 core_clk_state,link_clk_state,core_refcount,link_refcount;};
struct dsi_panel {bool initialized,ulps_suspend_enabled,ulps_feature;struct {bool force_hs_clk_lane;}host_config;};
struct dsi_display_ctrl {void*ctrl;};
struct dsi_display {struct dsi_panel*panel;bool clamp_enabled,phy_idle_power_off,ulps_enabled,is_cont_splash_enabled;int ctrl_count;struct dsi_display_ctrl ctrl[1];};
#define display_for_each_ctrl(i,d) for(i=0;i<(d)->ctrl_count;++i)
static bool dsi_panel_initialized(struct dsi_panel*p){return p->initialized;}
static bool dsi_panel_ulps_feature_enabled(struct dsi_panel*p){return p->ulps_feature;}
static int _dsi_display_continuous_clk_ctrl(struct dsi_display*d,bool b){event(b?"force-hs-on":"force-hs-off");return 0;}
static int dsi_display_set_ulps(struct dsi_display*d,bool b){event(b?"ulps-enter":"ulps-exit");d->ulps_enabled=b;return 0;}
static int dsi_display_config_clk_gating(struct dsi_display*d,bool b){event(b?"gating-on":"gating-off");return 0;}
static int dsi_display_phy_idle_off(struct dsi_display*d){event("phy-off");d->phy_idle_power_off=true;return 0;}
static int dsi_display_set_clamp(struct dsi_display*d,bool b){event(b?"clamp-on":"clamp-off");d->clamp_enabled=b;return 0;}
static int dsi_display_phy_reset_config(struct dsi_display*d,bool b){event(b?"reset-unmask":"reset-mask");return 0;}
static void dsi_display_ctrl_irq_update(struct dsi_display*d,bool b){event(b?"irq-on":"irq-off");}
static void dsi_ctrl_cache_misr(void*c){}
static int dsi_display_ctrl_setup(struct dsi_display*d){event("ctrl-setup");return 0;}
static int dsi_display_phy_idle_on(struct dsi_display*d,bool b){event("phy-on");d->phy_idle_power_off=false;return 0;}
static int dsi_display_toggle_resync_fifo(struct dsi_display*d){event("resync");return 0;}
static int dsi_link_hs_clk_set_rate(struct dsi_link_hs_clk_info*c,int i){event("hs-rates");return 0;}
static int dsi_link_lp_clk_start(struct dsi_link_lp_clk_info*c,int i){event("lp-on");return 0;}
static int dsi_link_lp_clk_stop(struct dsi_link_lp_clk_info*c){event("lp-off");return 0;}
static int dsi_display_core_clk_enable(struct dsi_core_clks*c,u32 n,u32 m){event("core-on");return 0;}
static int dsi_display_core_clk_disable(struct dsi_core_clks*c,u32 n,u32 m){event("core-off");return 0;}
static int pre_on(void*p,enum dsi_clk_type c,enum dsi_lclk_type l,enum dsi_clk_state s){return 0;}
'''
manager = 'drivers/gpu/drm/msm/dsi-staging/dsi_clk_manager.c'
display = 'drivers/gpu/drm/msm/dsi-staging/dsi_display.c'
code = prelude + ''.join(extract(display, n) for n in ('dsi_pre_clkoff_cb', 'dsi_post_clkon_cb'))
code += ''.join(extract(manager, n) for n in (
    'dsi_link_hs_clk_prepare', 'dsi_link_hs_clk_unprepare',
    'dsi_link_hs_clk_enable', 'dsi_link_hs_clk_disable',
    'dsi_link_hs_clk_start', 'dsi_link_hs_clk_stop',
    'dsi_display_link_clk_enable', 'dsi_display_link_clk_disable',
    'dsi_clk_update_link_clk_state', 'dsi_update_clk_state',
    'dsi_recheck_clk_state', 'dsi_clk_req_state'))
code += r'''
static void expect(const char*s){if(strcmp(events,s)){fprintf(stderr,"got: %s\nwant: %s\n",events,s);assert(false);}puts(events);events[0]=0;}
int main(void){
 struct clk byte={"prepare-byte","on-byte","off-byte","unprepare-byte"};
 struct clk pixel={"prepare-pixel","on-pixel","off-pixel","unprepare-pixel"};
 struct clk intf={"prepare-intf","on-intf","off-intf","unprepare-intf"};
 struct dsi_link_clks link={.hs_clks={&byte,&pixel,&intf}};
 struct dsi_core_clks core={0};
 struct dsi_panel panel={.ulps_suspend_enabled=true,.ulps_feature=true};
 struct dsi_display display={.panel=&panel,.ctrl_count=1};
 struct dsi_clk_mngr m={.priv_data=&display,.pre_clkon_cb=pre_on,.post_clkon_cb=dsi_post_clkon_cb,.pre_clkoff_cb=dsi_pre_clkoff_cb,.core_clks=&core,.link_clks=&link,.core_clk_state=DSI_CLK_ON,.link_clk_state=DSI_CLK_ON,.dsi_ctrl_count=1};
 struct dsi_clk_client_info dsi={.mngr=&m,.core_clk_state=DSI_CLK_ON,.link_clk_state=DSI_CLK_ON,.core_refcount=1,.link_refcount=1};
 struct dsi_clk_client_info mdp={.mngr=&m};
 m.client_list.next=&dsi.list;dsi.list.next=&mdp.list;mdp.list.next=&m.client_list;
 // Ordinary blank: clock manager chooses HS then LP callbacks, even though
 // the source body of pre_clkoff_cb lists its LP branch first.
 assert(!dsi_clk_req_state(&dsi,DSI_ALL_CLKS,DSI_CLK_OFF));
 expect("gating-off off-intf off-pixel off-byte unprepare-intf unprepare-pixel unprepare-byte ulps-enter lp-off phy-off clamp-on reset-mask irq-off core-off ");
 assert(!dsi_clk_req_state(&dsi,DSI_ALL_CLKS,DSI_CLK_ON));
 expect("core-on irq-on lp-on ctrl-setup phy-on ulps-enter reset-unmask clamp-off hs-rates prepare-byte prepare-pixel prepare-intf on-byte on-pixel on-intf resync ulps-exit gating-on ");
 // ON-table per-command votes nest inside prepare's held clock votes.
 for(unsigned n=0;n<17;++n){assert(!dsi_clk_req_state(&dsi,DSI_ALL_CLKS,DSI_CLK_ON));assert(!dsi_clk_req_state(&dsi,DSI_ALL_CLKS,DSI_CLK_OFF));assert(!events[0]);}
 assert(dsi.core_refcount==1&&dsi.link_refcount==1);
 // Independent MDP client keeps physical clocks running across DSI release.
 assert(!dsi_clk_req_state(&mdp,DSI_ALL_CLKS,DSI_CLK_ON));
 assert(!dsi_clk_req_state(&dsi,DSI_ALL_CLKS,DSI_CLK_OFF));assert(!events[0]);
 // An initialized panel uses normal ULPS enable, not suspend enable.
 panel.initialized=true;panel.ulps_suspend_enabled=false;
 assert(!dsi_clk_req_state(&mdp,DSI_ALL_CLKS,DSI_CLK_OFF));
 expect("gating-off off-intf off-pixel off-byte unprepare-intf unprepare-pixel unprepare-byte ulps-enter lp-off phy-off clamp-on reset-mask irq-off core-off ");
 puts("PASS: 14 unchanged LG functions; ordinary HS/LP callback order, HS branch order, nested DSI and aggregate MDP votes");
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-clock-reference-') as tmp:
    tmp = Path(tmp); (tmp / 'reference.c').write_text(code)
    subprocess.run([os.environ.get('CC', 'clang-19'), '-std=c11', '-g', '-fsanitize=address,undefined', str(tmp / 'reference.c'), '-o', str(tmp / 'reference')], check=True)
    output = subprocess.check_output([str(tmp / 'reference')], text=True,
        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    print(output, end='')
    if len(sys.argv) == 3:
        dest = Path(sys.argv[2]); dest.mkdir(parents=True, exist_ok=True)
        (dest / 'clock-reference.c').write_text(code)
        (dest / 'clock-reference.json').write_text(json.dumps(dict(pin=pin, scope=__doc__, sources=sources, functions=extracted, static_audit=static_audit, output=output), indent=2) + '\n')

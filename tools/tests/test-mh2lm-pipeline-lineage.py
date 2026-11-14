#!/usr/bin/env python3
"""Run unaltered LG Lineage helpers; compare/update the fixed-pipeline oracle.
Usage: test-mh2lm-pipeline-lineage.py LINEAGE_TREE [--write-fixture]
"""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,tempfile
from mh2lm_lineage_upper_flow import audit
root=Path(sys.argv[1]).resolve();directory=root/'drivers/gpu/drm/msm/sde'
upper=audit(root)
selection={
 'sde_hw_ctl.c':['sde_hw_ctl_poll_reset_status','sde_hw_ctl_reset_control','sde_hw_ctl_intf_cfg_v1','sde_hw_ctl_trigger_flush_v1'],
 'sde_hw_intf.c':['sde_hw_intf_bind_pingpong_blk','sde_hw_intf_vsync_sel','sde_hw_intf_setup_te_config','sde_hw_intf_enable_te','sde_hw_intf_setup_autorefresh_config','sde_hw_intf_connect_external_te'],
 'sde_hw_lm.c':['sde_hw_lm_setup_out','sde_hw_lm_setup_color3'],
 'sde_hw_pingpong.c':['sde_hw_pp_dsc_disable'],
 'sde_hw_sspp.c':['sde_hw_sspp_setup_pe_config']}
parts=[];hashes={}
for file,names in selection.items():
 text=(directory/file).read_text();hashes[file]=hashlib.sha256(text.encode()).hexdigest()
 defines=[];continuation=False
 for line in text.splitlines(keepends=True):
  if line.startswith('#define') or continuation:
   defines.append(line);continuation=line.rstrip().endswith('\\')
 parts.append(''.join(defines))
 for name in names:
  match=re.search(r'static\s+(?:inline\s+)?(?:int|void|u32)\s+'+name+r'\(',text);assert match,name
  brace=text.index('{',match.end());end=brace+1;depth=1
  while depth:
   depth+=(text[end]=='{')-(text[end]=='}');end+=1
  parts.append(text[match.start():end])
prelude=r'''
#include <cstdint>
#include <cstdio>
#include <map>
#include <cerrno>
using u32=uint32_t;using u8=uint8_t;using ktime_t=uint64_t;
#define BIT(n) (1u<<(n))
#define pr_debug(...) ((void)0)
static uint64_t ticks=0,resetDone=0;static bool stuck=false;static std::map<uint64_t,u32> regs;
struct sde_hw_blk_reg_map{uint64_t base;};
static u32 read(sde_hw_blk_reg_map*c,u32 off){if(off==0x30&&c->base==0x0ae02000&&!stuck&&ticks>=resetDone)regs[c->base+off]=0;return regs[c->base+off];}
static void write(sde_hw_blk_reg_map*c,u32 off,u32 v){regs[c->base+off]=v;if(off==0x30&&c->base==0x0ae02000)resetDone=ticks+50;printf("%llx %x\n",(unsigned long long)c->base+off,v);}
#define SDE_REG_READ(c,o) read(c,o)
#define SDE_REG_WRITE(c,o,v) write(c,o,v)
static ktime_t ktime_get(){return ticks;}static ktime_t ktime_add_us(ktime_t t,u32 n){return t+n;}
static int ktime_compare_safe(ktime_t a,ktime_t b){return (a>b)-(a<b);}static void usleep_range(u32 a,u32){ticks+=a;}
enum sde_pingpong{PINGPONG_0=1};
enum{INTF_0=1,WB_0=1,CWB_0=1,MERGE_3D_0=1,CDM_0=1,SDE_CTL_MODE_SEL_CMD=1,SDE_MAX_PLANES=4};
struct sde_ctl_flush_cfg{u32 pending_flush_mask,pending_wb_flush_mask,pending_dsc_flush_mask,pending_merge_3d_flush_mask,pending_cdm_flush_mask,pending_cwb_flush_mask,pending_intf_flush_mask,pending_periph_flush_mask;};
struct sde_hw_ctl{sde_hw_blk_reg_map hw;int idx;sde_ctl_flush_cfg flush;};
struct sde_hw_intf_cfg_v1{u32 intf_count,intf[6],intf_master,wb_count,wb[6],merge_3d_count,merge_3d[6],cwb_count,cwb[6],cdm_count,cdm[6],intf_mode_sel;};
struct sde_hw_intf{sde_hw_blk_reg_map hw;struct{bool split_link_en,pp_slave_intf;}cfg;};
struct sde_hw_tear_check{u32 hw_vsync_mode,vsync_count,sync_cfg_height,vsync_init_val,rd_ptr_irq,start_pos,sync_threshold_continue,sync_threshold_start;};
struct sde_hw_autorefresh{bool enable;u32 frame_count;};
struct sde_hw_mixer{sde_hw_blk_reg_map hw;};
struct sde_hw_mixer_cfg{u32 out_height,out_width;bool right_mixer;};
struct sde_hw_pingpong{sde_hw_blk_reg_map hw;};
struct sde_hw_pipe{sde_hw_blk_reg_map hw;};
struct sde_hw_pixel_ext{u32 right_ftch[4],right_rpt[4],left_ftch[4],left_rpt[4],btm_ftch[4],btm_rpt[4],top_ftch[4],top_rpt[4],roi_h[4],num_ext_pxls_top[4],num_ext_pxls_btm[4],roi_w[4],num_ext_pxls_left[4],num_ext_pxls_right[4];};
enum{SDE_SSPP_SRC=1};static int _sspp_subblk_offset(sde_hw_pipe*,int,u32*out){*out=0;return 0;}
'''
main=r'''
int main(){
 sde_hw_ctl ctl{{0x0ae02000},0,{}};
 sde_hw_pipe pipe{{0x0ae05000}};sde_hw_pixel_ext ext{};
 for(int n=0;n<4;++n){ext.roi_w[n]=1080;ext.roi_h[n]=2340;}
 sde_hw_sspp_setup_pe_config(&pipe,&ext);
 sde_hw_mixer mixer{{0x0ae45000}};sde_hw_mixer_cfg lm{2340,1080,false};
 sde_hw_lm_setup_out(&mixer,&lm);sde_hw_lm_setup_color3(&mixer,0);
 sde_hw_intf_cfg_v1 cfg{};cfg.intf_count=1;cfg.intf[0]=INTF_0+1;cfg.intf_mode_sel=SDE_CTL_MODE_SEL_CMD;
 sde_hw_ctl_intf_cfg_v1(&ctl,&cfg);
 sde_hw_pingpong pp{{0x0ae71000}};sde_hw_pp_dsc_disable(&pp);
 sde_hw_intf intf{{0x0ae6b800},{false,false}};
 sde_hw_intf_bind_pingpong_blk(&intf,true,PINGPONG_0);sde_hw_intf_vsync_sel(&intf,0);
 sde_hw_tear_check tc{};tc.vsync_count=19200000/(2360*60);tc.sync_cfg_height=0xfff0;
 tc.vsync_init_val=tc.start_pos=2340;tc.rd_ptr_irq=2341;tc.sync_threshold_start=tc.sync_threshold_continue=4;
 sde_hw_intf_setup_te_config(&intf,&tc);sde_hw_intf_enable_te(&intf,true);
 ctl.flush.pending_flush_mask=BIT(31)|BIT(17)|BIT(6)|BIT(0);ctl.flush.pending_intf_flush_mask=BIT(1);
 // Order supplied by the separately audited real upper callers.
 sde_hw_intf_connect_external_te(&intf,true);
 sde_hw_ctl_trigger_flush_v1(&ctl);
 sde_hw_autorefresh refresh{true,1};sde_hw_intf_setup_autorefresh_config(&intf,&refresh);
 // CTL reset helpers are a separate recovery self-test, never a normal enable.
 puts("RECOVERY");
 if(sde_hw_ctl_reset_control(&ctl)||ticks<50||ticks>2000)return 1;
 stuck=true;auto start=ticks;if(sde_hw_ctl_reset_control(&ctl)!=-EINVAL||ticks-start!=2000)return 2;
}
'''
with tempfile.TemporaryDirectory(prefix='mh2lm-lineage-pipeline-') as temp:
 source=Path(temp)/'reference.cpp';binary=Path(temp)/'reference'
 source.write_text(prelude+'\n'.join(parts)+main)
 subprocess.run([os.environ.get('CXX','clang++-19'),'-std=c++17','-fsanitize=address,undefined',str(source),'-o',str(binary)],check=True)
 output=subprocess.check_output([str(binary)],text=True,env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0",UBSAN_OPTIONS="halt_on_error=1"))
normal,recovery=output.split('RECOVERY\n')
trace=[dict(address=int(a,16),value=int(v,16)) for a,v in (line.split() for line in normal.splitlines())]
recovery_trace=[dict(address=int(a,16),value=int(v,16)) for a,v in (line.split() for line in recovery.splitlines())]
final={str(v['address']):v['value'] for v in trace if v['address']!=0x0ae02030}
record=dict(upper_flow=upper,reference_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),sources=hashes,helpers=selection,trace=trace,recovery_selftest_trace=recovery_trace,configured_registers=final)
fixture=Path(__file__).parent/'fixtures/mh2lm-pipeline-lineage.json'
if '--write-fixture' in sys.argv:fixture.write_text(json.dumps(record,indent=2)+'\n')
else:assert record==json.loads(fixture.read_text()),'Lineage source or reference trace changed'
print('PASS: executed',sum(map(len,selection.values())),'unaltered Lineage helpers; CTL 2ms reset bound, command routing, mixer, pixel extension, PP bypass, TE config; separate static upper-call audit; manually composed TE/flush/kickoff trace (not full DRM execution)')

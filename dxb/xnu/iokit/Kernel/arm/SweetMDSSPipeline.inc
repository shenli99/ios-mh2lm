/* PD2/PD4 first pipeline stage. LG Lineage 2e897dd, sm8150-sde.dtsi and
 * sde_hw_{sspp,lm,ctl,pingpong,intf}.c: VIG0 -> LM0 -> PP0 -> INTF1.
 * Software describes the fixed BGRA/unscaled command-mode path. No register
 * snapshot replay. Shared core/SMMU, VBIF/QoS and DSPP colour state stay held;
 * this is not a whole-MDP power-collapse or cold-boot implementation.
 * Same MDSS gate/timers and lifetime-pinned pool as the resource owner. */
bool SweetMDSS::setupPipeline(IOService *provider)
{
 const uint32_t abiVersion=1;
 OSData *abi=OSDynamicCast(OSData,provider->getProperty("sweet,display-pipeline-abi"));
 if (!abi || abi->getLength()!=4 || memcmp(abi->getBytesNoCopy(),&abiVersion,4)) return false;
 const uint64_t pa[]={0x0ae45000,0x0ae71000,0x0ae06a00};
 const uint64_t size[]={0x320,0xd4,4};
 for (unsigned n=0;n<3;++n) {
  fMaps[16+n]=provider->mapDeviceMemoryWithIndex(16+n);
  if (!fMaps[16+n] || fMaps[16+n]->getPhysicalAddress()!=pa[n] || fMaps[16+n]->getLength()!=size[n]) return false;
 }
 fPipelineEnabled=true;
 setProperty("SweetDisplayPipelineRebuild",true);
 SWEET_LOG_INFO("SWEET MDSS PIPELINE: mode7 Lineage VIG0/LM0/CTL0/PP0/INTF1 rebuild enabled; shared SMMU/core/QoS/colour retained\n");
 return true;
}

bool SweetMDSS::pipelineExclusive(void)
{
 /* DRM's resource manager allocates these blocks exclusively. Here the ABI
  * names one fixed path; reject another active CTL using INTF1, LM0 or VIG0.
  * Do not reset or clear another controller's routes. */
 for (unsigned ctl=1;ctl<6;++ctl) {
  vm_offset_t c=dp_ctl+ctl*0x200u;
  uint32_t intfs=dp_rd(c,0xf4),wb=dp_rd(c,0xec);
  if (intfs&2u) return false;
  if (!intfs && !wb) continue;
  for (unsigned lm=0;lm<6;++lm) {
   uint32_t a=dp_rd(c,lm==5 ? 0x24u : lm*4u),b=dp_rd(c,0x40u+lm*4u);
   uint32_t d=dp_rd(c,0x70u+lm*4u),e=dp_rd(c,0xa0u+lm*4u);
   if ((!lm && (a || b || d || e)) || (a&7u) || (b&1u) || (e&15u)) return false;
  }
 }
 return true;
}

IOReturn SweetMDSS::beginPipeline(void)
{
 if (!fPipelineEnabled || fPipelinePending || fPipelineGeneration==~0ULL ||
     !lifecycleParticipants() || !fLifecycle.touchQuiesced || !fLifecycle.displayDrained ||
     !lifecycleResourcesHeld() || !lifecycleIRQDrained() || dsiBusy() || fDsiPinned || fDsiPoisoned ||
     !fHardwarePool || !scanoutDomain() || !pipelineExclusive()) return kIOReturnNotReady;
 if ((dp_rd(dp_dsi,8)&6u) || dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c) ||
     (dp_rd(dp_ctl,0x30)&1u) || dp_rd(dp_intf1,0x2b4) || (dp_rd(dp_intf1,0x288)&(1u<<20))) return kIOReturnBusy;
 uint32_t line=dp_rd(dp_intf1,0x2b0)&0xffffu;
 if (line>0 && line<2340) return kIOReturnBusy;
 fPipelineConfigured=false;fLifecycle.pipelineGeneration=0;
 /* Independent hardware pin before programming can expose DMA. */
 if (!fScanoutTouched) {
  fHardwarePool->pinHardware();fScanoutTouched=true;
  fScanoutStatus.hardwareReachable=true;setProperty("SweetScanoutHardwarePinned",true);
 }
 /* Lineage command enable does not reset CTL. CRTC reset_req is an error
  * recovery branch, which this driver does not yet implement. Keep a gated
  * continuation so cancellation/participant loss still precedes programming. */
 fPipelinePending=true;
 SWEET_LOG_INFO("SWEET MDSS PIPELINE: normal command enable generation=%llu; no CTL reset\n",fPipelineGeneration+1);
 return kIOReturnSuccess;
}

IOReturn SweetMDSS::advancePipeline(void)
{
 if (!fPipelinePending || !lifecycleParticipants() || !lifecycleResourcesHeld() ||
     !lifecycleIRQDrained() || !fLifecycle.touchQuiesced || !fLifecycle.displayDrained ||
     dsiBusy() || fDsiPinned || fDsiPoisoned || !scanoutDomain()) return kIOReturnNotReady;
 if ((dp_rd(dp_dsi,8)&6u) || (dp_rd(dp_ctl,0x30)&1u) ||
     dp_rd(dp_ctl,0x18) || dp_rd(dp_ctl,0x1c) || dp_rd(dp_intf1,0x2b4) ||
     (dp_rd(dp_intf1,0x288)&(1u<<20))) return kIOReturnNotReady;
 if (!pipelineExclusive()) return kIOReturnNotReady;
 fPipelinePending=false;
 const vm_offset_t p=dp_sspp,c=dp_ctl,i=dp_intf1;
 const vm_offset_t lm=fMaps[16]->getVirtualAddress(),pp=fMaps[17]->getVirtualAddress();
 const uint32_t size=(2340u<<16)|1080u;
 /* Lineage format/rect/multirect/pixel-extension and scaler bypass. */
 dp_wr(fMaps[18]->getVirtualAddress(),0,dp_rd(fMaps[18]->getVirtualAddress(),0)&~3u);
 dp_wr(p,0xa04,0); /* QSEED3 disabled for 1:1 RGB. */
 dp_wr(p,0x30,0x237ffu);dp_wr(p,0x34,0x03020001u);dp_wr(p,0x38,1u<<31);
 dp_wr(p,0x70,0);dp_wr(p,0x138,1u<<31); /* non-secure, UBWC error clear */
 dp_wr(p,0x170,0);dp_wr(p,0xb4,0);
 dp_wr(p,0,size);dp_wr(p,8,0x00300016u);dp_wr(p,0xc,size);dp_wr(p,0x10,0);
 dp_wr(p,0x24,4608);dp_wr(p,0x28,0);
 for (unsigned n=0;n<3;++n) {
  dp_wr(p,0x100+n*0x10,0);dp_wr(p,0x104+n*0x10,0);dp_wr(p,0x108+n*0x10,size);
 }
 dp_wr(p,0x14,(uint32_t)fHardwarePool->physical(0));
 dp_wr(p,0x18,0);dp_wr(p,0x1c,0);dp_wr(p,0x20,0);
 /* One base layer, no split mixer, alpha blend, dim layers or DSC. */
 dp_wr(lm,4,size);dp_wr(lm,0,0);
 for (unsigned n=0;n<11;++n) dp_wr(lm,0x20+n*0x18,0);
 for (unsigned n=0;n<6;++n) {
  dp_wr(c,n==5 ? 0x24u : n*4u,n==0 ? 1u : 0u);
  dp_wr(c,0x40+n*4,0);dp_wr(c,0x70+n*4,0);dp_wr(c,0xa0+n*4,0);
 }
 dp_wr(c,0x14,1u<<17);dp_wr(c,0xec,0);dp_wr(c,0xf0,0);dp_wr(c,0xf4,2);
 dp_wr(c,0xf8,0);dp_wr(c,0xe4,0);dp_wr(c,0x134,0);dp_wr(c,0xe8,0);
 dp_wr(pp,0xc8,dp_rd(pp,0xc8)&~(1u<<18));dp_wr(pp,0xa0,0);
 dp_wr(i,0x25c,dp_rd(i,0x25c)&~15u); /* bind PP0 */
 /* Four R66456 DTS profiles: 1080x2340, vtotal=2360, 60Hz, TE0.
  * 19.2MHz/(2360*60)=135, derived as in encoder_phys_cmd tearcheck.
  * External TE is connected by encoder enable BEFORE commit/kickoff. */
 dp_wr(i,0x280,0);dp_wr(i,0x2b4,0);dp_wr(i,0x288,(1u<<19)|135u);
 dp_wr(i,0x28c,0xfff0);dp_wr(i,0x294,2340);dp_wr(i,0x2a4,2341);
 dp_wr(i,0x2a0,2340);dp_wr(i,0x29c,0x00040004);dp_wr(i,0x290,2345);dp_wr(i,0x284,1);
 fPipelineConfigured=true;++fPipelineGeneration;
 fLifecycle.pipelineGeneration=fPipelineGeneration;
 setProperty("SweetDisplayPipelineGeneration",(unsigned long long)fPipelineGeneration,64);
 SWEET_LOG_INFO("SWEET MDSS PIPELINE: configured generation=%llu source=%#llx 1080x2340 stride=4608 crop=22,48; fresh frame still required\n",fPipelineGeneration,fHardwarePool->physical(0));
 return kIOReturnSuccess;
}

void SweetMDSS::kickoffPipeline(void)
{
 /* ACTIVE_CFG uses an INTF sub-mask and top-level INTF/CTL/LM/VIG flush.
  * Autorefresh frame_count=1 is software policy, never a saved ABL value. */
 /* msm_atomic modeset enables -> encoder enable/control_te -> kms commit. */
 dp_wr(dp_intf1,0x288,(1u<<20)|(1u<<19)|135u);
 dp_wr(dp_ctl,0x110,2u);
 dp_wr(dp_ctl,0x18,(1u<<31)|(1u<<17)|(1u<<6)|1u);
 dp_wr(dp_intf1,0x2b4,0x80000001u);
 SWEET_LOG_INFO("SWEET MDSS PIPELINE: generation=%llu TE connected before flush/kickoff; waiting for fresh PP_DONE and RD_PTR\n",fPipelineGeneration);
}

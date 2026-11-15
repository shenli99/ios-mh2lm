/* F4 entry: real owned buffers and read-only hardware evidence. No flip yet. */
bool
AppleCLCD::setupScanoutPool(void)
{
 /* Refuse geometry drift rather than making an unbounded read from ABL. */
 if (fWidth!=1080 || fHeight!=2340 || fRowBytes!=4320 ||
     fLogicalW!=1125 || fLogicalH!=2436 || fOffX!=22 || fOffY!=48 ||
     fPhys!=0x9d200000ULL || !fPixels) {
  SWEET_LOG_ERROR("SWEET FB SCANOUT: unexpected ABL geometry pa=%#llx panel=%ux%u stride=%u canvas=%ux%u crop=%d,%d\n",
      fPhys,fWidth,fHeight,fRowBytes,fLogicalW,fLogicalH,fOffX,fOffY); return false;
 }
 SweetScanoutPool *pool=SweetScanoutPool::withGeometry(fLogicalW,fLogicalH,4608);
 if (!pool) return false;
 const uint64_t sourceBytes=(uint64_t)fRowBytes*fHeight;
 if (!fSurface || !fSurface->memory || fSurface->memory->getLength()<sourceBytes) {
  SWEET_LOG_ERROR("SWEET FB SCANOUT: ABL source descriptor too small\n");
  pool->release(); return false;
 }
 for (unsigned n=0;n<2;++n) {
  uint64_t pa=pool->physical(n);
  /* Preserve the entire conservative 18MiB display reservation, not only
   * the currently visible 1080x2340 image. */
  if (pa<fPhys+0x1200000ULL && fPhys<pa+pool->bytes()) {
   SWEET_LOG_ERROR("SWEET FB SCANOUT: buffer index=%u aliases ABL reservation pa=%#llx\n",n,pa);
   pool->release(); return false;
  }
 }
 uint64_t start=mach_absolute_time(),elapsed;
 bool seeded=pool->seedABL((const void *)fPixels,(size_t)sourceBytes,
     fWidth,fHeight,fRowBytes,(uint32_t)fOffX,(uint32_t)fOffY);
 absolutetime_to_nanoseconds(mach_absolute_time()-start,&elapsed);
 if (!seeded) { pool->release(); return false; }
 fScanoutPool=pool;
 setProperty("SweetScanoutPreflight",true);
 setProperty("SweetScanoutActive",false);
 setProperty("SweetScanoutBufferA",(unsigned long long)pool->physical(0),64);
 setProperty("SweetScanoutBufferB",(unsigned long long)pool->physical(1),64);
 setProperty("SweetScanoutBufferBytes",(unsigned long long)pool->bytes(),64);
 SWEET_LOG_INFO("SWEET FB SCANOUT: F4 v1 pool ready A=%#llx B=%#llx bytes=%llu stride=4608 crop=22,48; contiguous32/WC; DMA reachability unproven\n",
     pool->physical(0),pool->physical(1),(uint64_t)pool->bytes());
 SWEET_LOG_INFO("SWEET FB SCANOUT: seeded A from ABL %ux%u stride=%u elapsed-us=%llu; scan address untouched\n",
     fWidth,fHeight,fRowBytes,elapsed/1000ULL);
 return true;
}
void
AppleCLCD::auditScanout(void)
{
 if ((fOwnScanoutStarted && !fOwnScanoutActive) || !fOwnScanoutPreflight || fServiceStopped || fPowerOff || !fVsyncBackend || fScanoutAuditCount>=4) return;
 ++fScanoutAuditCount; /* absolute lifetime budget, including failed snapshots */
 SweetScanoutSnapshot v={};
 IOReturn rc=fVsyncBackend->inspectScanout(&v);
 if (rc!=kIOReturnSuccess) {
  SWEET_LOG_ERROR("SWEET FB SCANOUT: inspect pass=%u rc=%#x; scan address untouched\n",fScanoutAuditCount,(unsigned)rc);
  setProperty("SweetScanoutPreflightFailed",true);
  if (fOwnScanoutActive) {
   fOwnScanoutFailed=true; setProperty("SweetScanoutHandoffFailed",true); setProperty("SweetScanoutActive",false);
   if (fScanoutBackend) fScanoutBackend->abandonScanout(fScanoutPool);
  }
  return;
 }
 setProperty("SweetScanoutPreflightSamples",(unsigned long long)fScanoutAuditCount,32);
 SWEET_LOG_INFO("SWEET FB SCANOUT: inspect pass=%u frame=%llu ticks=%llu size=%#x xy=%#x out=%#x outxy=%#x addr=%#x stride=%#x fmt=%#x unpack=%#x op=%#x secure=%#x\n",
     fScanoutAuditCount,v.frame,v.ticks,v.size,v.xy,v.out,v.outXY,v.address,v.stride,v.format,v.unpack,v.op,v.secure);
 SWEET_LOG_INFO("SWEET FB SCANOUT: ctl flush=%#x start=%#x top=%#x intf-cfg=%#x status=%#x te=%#x auto=%#x wr-count=%#x line=%#x out-line=%#x mdp-en=%#x mdp-status=%#x tear-en=%#x tear-status=%#x\n",
     v.flush,v.start,v.ctlTop,v.intfCfg,v.intfStatus,v.te,v.autoRefresh,v.wrCount,v.line,v.outLine,
     v.mdpEnable,v.mdpStatus,v.tearEnable,v.tearStatus);
 SWEET_LOG_INFO("SWEET FB SCANOUT: smmu id0=%#x id1=%#x known=%u cr0=%#x fault=%#x matches=%u idx=%u smr=%#x s2cr=%#x cb2-observed=%u\n",
     v.smmuID0,v.smmuID1,v.knownSmmu,v.smmuCR0,v.smmuFault,v.matches,v.streamIndex,v.smr,v.s2cr,v.cb2Observed);
 if (v.cb2Observed)
  SWEET_LOG_INFO("SWEET FB SCANOUT: cb2 cbar=%#x cba2r=%#x sctlr=%#x ttbcr=%#x ttbr=%08x:%08x mair=%08x:%08x fsr=%#x far=%08x:%08x\n",
      v.cbar,v.cba2r,v.sctlr,v.ttbcr,v.ttbrHi,v.ttbrLo,v.mairHi,v.mairLo,v.cbFault,v.farHi,v.farLo);
 bool layoutOK=v.address==(fOwnScanoutActive ? fScanoutPool->physical(0) : fPhys) &&
     v.stride==(fOwnScanoutActive ? 4608u : fRowBytes) && v.xy==(fOwnScanoutActive ? 0x00300016u : 0u) &&
     v.size==0x09240438u && v.out==0x09240438u && !v.outXY && v.format==0x237ffu &&
     v.unpack==0x03020001u && v.op==0x80000000u && !v.secure;
 bool routeOK=v.knownSmmu && v.matches==1 && v.cb2Observed && !v.smmuFault &&
     !(v.cbFault&~0x400u) && v.sctlr==0xe0u && v.cbar==0x1f000u && v.cba2r==1u;
 if (fOwnScanoutActive) {
  if (!layoutOK || !routeOK) {
   fOwnScanoutFailed=true; setProperty("SweetScanoutHandoffFailed",true); setProperty("SweetScanoutActive",false);
   if (fScanoutBackend) fScanoutBackend->abandonScanout(fScanoutPool);
   SWEET_LOG_ERROR("SWEET FB HANDOFF: FAILED owned resume layout=%u route=%u; pool retained, pixels frozen\n",layoutOK,routeOK);
  } else SWEET_LOG_INFO("SWEET FB HANDOFF: owned resume verified A=%#llx frame=%llu crop=22,48 raw-fsr=%#x\n",
      (uint64_t)v.address,v.frame,v.cbFault);
 } else if (!layoutOK || !routeOK || v.cbFault)
  SWEET_LOG_WARN("SWEET FB SCANOUT: live configuration needs review; no handoff authorized\n");
 /* Even known CB2.M=0 and clean status do not prove a new DMA range reachable.
  * +0x70 is security metadata. RD_PTR/ADDR reads are not a release fence. */
}

/* Existing framebuffer workloop/gate, never a matching callback. The one-time
 * refresh replaces the boot seed with CURRENT panel pixels while presentation
 * is serialized; subsequent swaps remain pending until handoff resolves. */
void
AppleCLCD::scanoutStep(void)
{
 if (!fOwnScanoutRequested || fServiceStopped || fOwnScanoutFailed) return;
 if (!fOwnScanoutStarted) {
  if (!fScanoutPool || fScanoutAuditCount<3 || !fHardwareVsyncActive || fPowerOff || !fVsyncBackend) return;
  fOwnScanoutStarted=true; /* one attempt; every rejection is explicit */
  sweet_console_muted=1;
  if (!fScanoutPool->refreshPanel((const void *)fPixels,(size_t)fRowBytes*fHeight)) {
   fOwnScanoutFailed=true; setProperty("SweetScanoutHandoffFailed",true);
   SWEET_LOG_ERROR("SWEET FB HANDOFF: FAILED current-frame refresh\n"); return;
  }
  __asm__ volatile("dsb oshst" ::: "memory");
  fTrace=false; /* old trace compares against ABL; do not manufacture proof */
  fVsyncBackend->retain(); fScanoutBackend=fVsyncBackend;
  IOReturn rc=fScanoutBackend->beginScanout(fScanoutPool,fVsyncChannel);
  if (rc!=kIOReturnSuccess) {
   fOwnScanoutFailed=true; setProperty("SweetScanoutHandoffFailed",true);
   SWEET_LOG_ERROR("SWEET FB HANDOFF: FAILED begin rc=%#x; no silent ABL presentation\n",(unsigned)rc); return;
  }
  SWEET_LOG_INFO("SWEET FB HANDOFF: F4 v2 accepted; current frame refreshed; awaiting provider transfer; single-buffer/30Hz\n");
 }
 SweetScanoutStatus v={};
 uint64_t queryStart=fPerf.enabled ? sweet_fb_trace_now() : 0;
 IOReturn rc=fScanoutBackend->scanoutStatus(fScanoutPool,&v);
 if (fPerf.enabled) fPerf.query.add(sweet_fb_trace_now()-queryStart,1000000ULL);
 if (rc!=kIOReturnSuccess || v.state==SweetScanoutFailed) {
  fOwnScanoutFailed=true; setProperty("SweetScanoutHandoffFailed",true);
  setProperty("SweetScanoutActive",false);
  SWEET_LOG_ERROR("SWEET FB HANDOFF: FAILED provider rc=%#x state=%u hardware-reachable=%u; pixels frozen, pool retained\n",
      (unsigned)rc,v.state,v.hardwareReachable); return;
 }
 if (v.state==SweetScanoutActive && !fOwnScanoutActive) {
  fOwnScanoutActive=true; fDirty=true;
  setProperty("SweetScanoutActive",true);
  setProperty("SweetScanoutAddress",(unsigned long long)v.address,64);
  SWEET_LOG_INFO("SWEET FB HANDOFF: active A=%#llx frame=%llu fresh-pp-done=%#x; CPU copies now target owned A; 30Hz/in-place, no flip fence\n",
      v.address,v.frame,v.done);
 }
}
void
AppleCLCD::shutdownScanout(void)
{
 if (fScanoutBackend) fScanoutBackend->abandonScanout(fScanoutPool);
 OSSafeReleaseNULL(fScanoutBackend);
 fOwnScanoutActive=false;
 setProperty("SweetScanoutActive",false);
}

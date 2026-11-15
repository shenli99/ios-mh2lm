/* Caller owns the FB workloop gate; no allocation, sorting or per-frame log. */
void
AppleCLCD::reportPerf(uint64_t now)
{
 if (!fPerf.enabled) return;
 if (!fPerf.nextReportNS) { fPerf.nextReportNS=now+15000000000ULL; return; }
 if (now<fPerf.nextReportNS) return;
 SWEET_LOG_INFO("SWEET FB PERF: v1 own-active=%u swaps=%llu coalesced=%llu cpu-presents=%llu copy-bytes=%llu; IRQ is not presentation\n",
     fOwnScanoutActive,fPerf.swaps,fPerf.coalesced,fPerf.presents,fPerf.bytes);
 const SweetFBTiming *values[]={&fPerf.copy,&fPerf.present,&fPerf.query,&fPerf.pending,&fPerf.latest,&fPerf.interval};
 const char *names[]={"copy+barrier","cpu-present","provider-query","pending-first","pending-latest","cpu-interval"};
 for (unsigned i=0;i<6;++i) {
  const SweetFBTiming &t=*values[i];
  SWEET_LOG_INFO("SWEET FB PERF: %s n=%llu total-us=%llu p50-upper-us=%llu p99-upper-us=%llu max-us=%llu over-budget=%llu\n",
      names[i],t.count,t.totalNS/1000ULL,t.percentile(50),t.percentile(99),t.maxNS/1000ULL,t.overBudget);
 }
 fPerf.copy={}; fPerf.present={}; fPerf.query={}; fPerf.pending={}; fPerf.latest={}; fPerf.interval={};
 fPerf.swaps=fPerf.coalesced=fPerf.bytes=fPerf.presents=0;
 fPerf.nextReportNS=now+15000000000ULL;
}

/* Low-frequency CPU presentation evidence. Not a hardware flip/reuse fence.
 * Fixed logarithmic microsecond bins; p50/p99 are bucket UPPER bounds.
 */
#ifndef SWEET_FB_PERF_H
#define SWEET_FB_PERF_H
#include <stdint.h>
struct SweetFBTiming {
 uint64_t count, totalNS, maxNS, overBudget;
 uint64_t buckets[32];
 void add(uint64_t ns,uint64_t budget) {
  ++count; totalNS+=ns;
  if (ns>maxNS) maxNS=ns;
  if (ns>budget) ++overBudget;
  uint64_t us=ns/1000u+(ns%1000u!=0); unsigned b=0;
  while (b<31 && us>(1ULL<<b)) ++b;
  ++buckets[b];
 }
 uint64_t percentile(unsigned percent) const {
  if (!count) return 0;
  uint64_t target=(count/100u)*percent+((count%100u)*percent+99u)/100u, sum=0;
  for (unsigned b=0;b<32;++b) { sum+=buckets[b]; if (sum>=target) return b==31 ? maxNS/1000u+(maxNS%1000u!=0) : 1ULL<<b; }
  return 1ULL<<31;
 }
};
struct SweetFBPerf {
 bool enabled;
 uint64_t nextReportNS, firstPendingNS, latestPendingNS, lastPresentNS;
 uint64_t swaps, coalesced, bytes, presents;
 SweetFBTiming copy, present, query, pending, latest, interval;
};
#endif

#ifndef SWEET_DSI_LINK_H
#define SWEET_DSI_LINK_H
/* Fixed DPHY topology: VCO / 2 -> bit; bit / 8 -> byte; byte / 2 -> byte-intf; bit / 6 -> pixel.
 * 19.2MHz XO doubled internally, 18 fractional bits; LG 7nm v2 arithmetic.
 * No hardware access, rounding up of an unsupported rate, or arbitrary dividers. */
struct SweetDSILinkPlan {
 uint64_t bitHz, vcoHz, actualVcoHz;
 uint32_t decimal, fraction, sscPeriod, sscStep;
};
static inline bool sweet_dsi_link_plan(uint64_t bitHz,SweetDSILinkPlan *out)
{
 if (!out || bitHz<500000000ULL || bitHz>=1500000000ULL) return false;
 SweetDSILinkPlan p={}; p.bitHz=bitHz; p.vcoHz=bitHz*2;
 uint64_t units=p.vcoHz*262144ULL/38400000ULL;
 p.decimal=(uint32_t)(units>>18); p.fraction=(uint32_t)(units&0x3ffffu);
 if (!p.decimal || p.decimal>255) return false;
 p.actualVcoHz=units*38400000ULL/262144ULL;
 /* LG defaults: 31.5kHz, 5000ppm DOWN spread, ADJPER=2. */
 p.sscPeriod=((19200000u+31500u/2u)/31500u)/2u-1u;
 p.sscPeriod-=(p.sscPeriod+1u)%3u;
 p.sscStep=(uint32_t)((units*5000ULL*3ULL/(p.sscPeriod+1u)+500000ULL)/1000000ULL);
 if (!p.sscStep || p.sscStep>65535u) return false;
 *out=p; return true;
}
#endif

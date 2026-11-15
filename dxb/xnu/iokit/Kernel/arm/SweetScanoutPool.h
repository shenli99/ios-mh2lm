/* F4 owned scanout storage. This object does not program hardware. */
#ifndef SWEET_SCANOUT_POOL_H
#define SWEET_SCANOUT_POOL_H
#if defined(ARM64_BOARD_CONFIG_MH2LM)
#include <IOKit/IOBufferMemoryDescriptor.h>
class SweetScanoutPool : public OSObject {
 OSDeclareDefaultStructors(SweetScanoutPool);
 IOBufferMemoryDescriptor *fBuffers[2];
 bool fPrepared[2];
 uint64_t fPhysical[2];
 uint32_t fWidth, fHeight, fStride;
 size_t fBytes;
 bool fHardwarePinned;
public:
 static SweetScanoutPool *withGeometry(uint32_t,uint32_t,uint32_t);
 bool seedABL(const void *,size_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
 uint64_t physical(unsigned n) const { return n<2 ? fPhysical[n] : 0; }
 size_t bytes(void) const { return fBytes; }
 uint32_t width(void) const { return fWidth; }
 uint32_t height(void) const { return fHeight; }
 uint32_t stride(void) const { return fStride; }
 void *address(unsigned n) const { return n<2 && fBuffers[n] ? fBuffers[n]->getBytesNoCopy() : NULL; }
 bool refreshPanel(const void *,size_t);
 /* Experimental stage2 never releases this pin: no proven hardware detach
  * protocol yet. At most one pool can reach hardware per boot. This explicit
  * quarantine survives both provider and framebuffer termination. */
 void pinHardware(void) { if (!fHardwarePinned) { retain(); fHardwarePinned=true; } }
 bool hardwarePinned(void) const { return fHardwarePinned; }
 void free(void) APPLE_KEXT_OVERRIDE;
};
#endif
#endif

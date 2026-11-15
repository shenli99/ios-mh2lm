/* Early thread-context allocation, before the GUI is published. No heap
 * allocation or >10MiB copy in a vsync/notification callback or spin lock. */
OSDefineMetaClassAndStructors(SweetScanoutPool,OSObject);
SweetScanoutPool *
SweetScanoutPool::withGeometry(uint32_t width,uint32_t height,uint32_t stride)
{
 if (!width || !height || width>4096 || height>4096 || (stride&63u) ||
     stride<(uint64_t)width*4u || stride>65536u) return NULL;
 const uint64_t bytes=(uint64_t)stride*height;
 if (!bytes || bytes>64u*1024u*1024u) return NULL;
 SweetScanoutPool *s=OSTypeAlloc(SweetScanoutPool);
 if (!s) return NULL;
 if (!s->init()) { s->release(); return NULL; }
 s->fWidth=width; s->fHeight=height; s->fStride=stride; s->fBytes=(size_t)bytes;
 for (unsigned n=0;n<2;++n) {
  s->fBuffers[n]=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
      kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMemoryMapperNone |
      kIOMapWriteCombineCache,bytes,0xffffffffULL);
  if (!s->fBuffers[n] || s->fBuffers[n]->getLength()<bytes ||
      !s->fBuffers[n]->getBytesNoCopy()) {
   SWEET_LOG_ERROR("SWEET FB SCANOUT: buffer index=%u allocation/mapping failed bytes=%llu\n",n,bytes);
   s->release(); return NULL;
  }
  IOReturn rc=s->fBuffers[n]->prepare(kIODirectionInOut);
  if (rc!=kIOReturnSuccess) {
   SWEET_LOG_ERROR("SWEET FB SCANOUT: buffer index=%u prepare failed rc=%#x\n",n,(unsigned)rc);
   s->release(); return NULL;
  }
  s->fPrepared[n]=true;
  IOByteCount contiguous=0;
  uint64_t pa=s->fBuffers[n]->getPhysicalSegment(0,&contiguous,kIOMemoryMapperNone);
  if (!pa || (pa&4095u) || pa>=0x100000000ULL || bytes>0x100000000ULL-pa || contiguous<bytes) {
   SWEET_LOG_ERROR("SWEET FB SCANOUT: buffer index=%u physical check failed pa=%#llx segment=%llu bytes=%llu\n",
       n,pa,(uint64_t)contiguous,bytes);
   s->release(); return NULL;
  }
  s->fPhysical[n]=pa;
 }
 if (s->fPhysical[0]<s->fPhysical[1]+bytes && s->fPhysical[1]<s->fPhysical[0]+bytes) {
  SWEET_LOG_ERROR("SWEET FB SCANOUT: buffer physical ranges overlap\n");
  s->release(); return NULL;
 }
 return s;
}
bool
SweetScanoutPool::seedABL(const void *source,size_t length,uint32_t width,uint32_t height,
    uint32_t stride,uint32_t x,uint32_t y)
{
 /* Source is the physical panel rectangle, NOT the larger logical canvas.
  * Validate every bound before touching memory; only destination A is written.
  * The caller also proves the physical source and both buffers do not alias. */
 if (!source || !width || !height || x>fWidth || y>fHeight ||
     width>fWidth-x || height>fHeight-y || stride<(uint64_t)width*4u ||
     (uint64_t)(height-1)*stride+(uint64_t)width*4u>length) return false;
 /* Do not initialize storage in withGeometry(): the caller must first
  * reject overlap with the whole display reservation, before ANY pixel write. */
 bzero(fBuffers[0]->getBytesNoCopy(),fBytes);
 bzero(fBuffers[1]->getBytesNoCopy(),fBytes);
 uint8_t *dst=(uint8_t *)fBuffers[0]->getBytesNoCopy();
 const uint8_t *src=(const uint8_t *)source;
 for (uint32_t row=0;row<height;++row)
  memcpy(dst+(size_t)(y+row)*fStride+(size_t)x*4u,src+(size_t)row*stride,(size_t)width*4u);
 return true;
}
bool
SweetScanoutPool::refreshPanel(const void *source,size_t length)
{
 if (fHardwarePinned || !source || fWidth!=1125 || fHeight!=2436 || fStride!=4608 || length<4320u*2340u) return false;
 uint8_t *dst=(uint8_t *)address(0);
 for (unsigned y=0;y<2340;++y)
  memcpy(dst+(size_t)(y+48)*4608+88,(const uint8_t *)source+(size_t)y*4320,4320);
 return true;
}
void
SweetScanoutPool::free(void)
{
 /* own1 is never submitted; own2's independent hardware pin prevents free
  * even when both services terminate. Do not remove that pin on a timeout. */
 for (unsigned n=0;n<2;++n) {
  if (fPrepared[n]) { (void)fBuffers[n]->complete(kIODirectionInOut); fPrepared[n]=false; }
  OSSafeReleaseNULL(fBuffers[n]);
 }
 OSObject::free();
}

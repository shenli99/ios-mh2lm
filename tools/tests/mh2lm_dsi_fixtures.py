"""OS memory objects only; DSI state machine/packet encoding remain real code."""
DSI_MEMORY_FIXTURE = r'''
using IOByteCount=uint64_t;
constexpr unsigned kIODirectionOut=2,kIOMemoryPhysicallyContiguous=0x10,
 kIOMemoryMapperNone=0x800,kIOMapWriteCombineCache=0x400;
constexpr uintptr_t kernel_task=1;
static unsigned dsiMemoryFault=0,dsiPrepares=0,dsiCompletes=0;
struct IOBufferMemoryDescriptor : OSObject {
 std::vector<uint8_t> bytes;bool prepared=false;uint64_t pa=0x18000000;
 static IOBufferMemoryDescriptor *inTaskWithPhysicalMask(uintptr_t task,unsigned options,uint64_t length,uint64_t mask){
  assert(task==kernel_task&&length==4096&&mask==0xffffffffULL);
  assert(options==(kIODirectionOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone|kIOMapWriteCombineCache));
  if(dsiMemoryFault==1)return nullptr;
  auto*p=new IOBufferMemoryDescriptor;p->bytes.resize(4096,0xab);
  if(dsiMemoryFault==5)p->pa=0x9d210000;
  if(dsiMemoryFault==6)p->pa=0x100000000ULL;
  if(dsiMemoryFault==7)++p->pa;
  return p;
 }
 void*getBytesNoCopy(){return dsiMemoryFault==2?nullptr:bytes.data();}
 uint64_t getLength(){return bytes.size();}
 IOReturn prepare(unsigned d){assert(d==kIODirectionOut&&!prepared);if(dsiMemoryFault==3)return kIOReturnNoMemory;
  prepared=true;++dsiPrepares;return 0;}
 IOReturn complete(unsigned d){assert(d==kIODirectionOut&&prepared);prepared=false;++dsiCompletes;return 0;}
 uint64_t getPhysicalSegment(uint64_t off,IOByteCount*n,unsigned options){assert(prepared&&!off&&options==kIOMemoryMapperNone);
  *n=dsiMemoryFault==4?128:bytes.size();return pa;}
 void free()override{assert(!prepared);OSObject::free();}
};
struct OSData : OSObject {
 std::vector<uint8_t> bytes;
 static OSData*withBytes(const void*p,unsigned n){auto*d=new OSData;d->bytes.assign((const uint8_t*)p,(const uint8_t*)p+n);return d;}
 const void*getBytesNoCopy(){return bytes.data();}
 unsigned getLength(){return (unsigned)bytes.size();}
};
'''

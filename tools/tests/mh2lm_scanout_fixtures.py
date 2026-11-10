"""OS allocation fixtures shared by actual F4 pool and framebuffer tests."""
BUFFER_FIXTURE = r'''
using IOByteCount=uint64_t;
constexpr unsigned kIODirectionInOut=3,kIOMemoryPhysicallyContiguous=0x10,
 kIOMemoryMapperNone=0x800,kIOMapWriteCombineCache=0x400;
constexpr uintptr_t kernel_task=1;
static unsigned scanoutFault=0,scanoutAllocCalls=0,scanoutPrepares=0,scanoutCompletes=0;
static std::vector<struct IOBufferMemoryDescriptor*> scanoutBuffers;
struct IOBufferMemoryDescriptor : IOMemoryDescriptor {
 std::vector<uint8_t> storage;uint64_t pa=0;bool wired=false;unsigned ordinal=0;
 static IOBufferMemoryDescriptor*inTaskWithPhysicalMask(uintptr_t task,unsigned options,uint64_t bytes,uint64_t mask){
  assert(task==kernel_task&&mask==0xffffffffULL);
  assert(options==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone|kIOMapWriteCombineCache));
  unsigned n=++scanoutAllocCalls;
  if((scanoutFault==1&&n==1)||(scanoutFault==2&&n==2))return nullptr;
  auto*p=new IOBufferMemoryDescriptor;p->ordinal=n;p->length=bytes;
  p->storage.resize(bytes+128,0xab);p->pa=0x10000000ULL+uint64_t(n-1)*0x4000000ULL;
  if(scanoutFault==5)p->length=bytes-1;
  if(scanoutFault==8)p->pa=0;
  if(scanoutFault==9)p->pa=0x100000000ULL;
  if(scanoutFault==10)p->pa=0xfffff000ULL;
  if(scanoutFault==11)++p->pa;
  if(scanoutFault==12)p->pa=0x10000000;
  if(scanoutFault==13)p->pa=0x9d210000ULL+uint64_t(n-1)*0x4000000ULL;
  if(scanoutFault==14)p->pa=0x9dc00000ULL+uint64_t(n-1)*0x4000000ULL;
  scanoutBuffers.push_back(p);return p;
 }
 void*getBytesNoCopy(){return scanoutFault==6?nullptr:storage.data()+64;}
 IOReturn prepare(unsigned direction){assert(direction==kIODirectionInOut&&!wired);
  if((scanoutFault==3&&ordinal==1)||(scanoutFault==4&&ordinal==2))return kIOReturnNoMemory;
  wired=true;++scanoutPrepares;return 0;}
 IOReturn complete(unsigned direction){assert(direction==kIODirectionInOut&&wired);wired=false;++scanoutCompletes;return 0;}
 uint64_t getPhysicalSegment(uint64_t offset,IOByteCount*contiguous,unsigned options){
  assert(offset==0&&options==kIOMemoryMapperNone&&wired);*contiguous=scanoutFault==7?length-1:length;return pa;
 }
 void free()override{
  assert(!wired);
  if(scanoutFault==13||scanoutFault==14)for(auto byte:storage)assert(byte==0xab); // no pixel writes before alias rejection
  for(unsigned i=0;i<64;++i)assert(storage[i]==0xab&&storage[storage.size()-1-i]==0xab);
  scanoutBuffers.erase(std::find(scanoutBuffers.begin(),scanoutBuffers.end(),this));
  IOMemoryDescriptor::free();
 }
};
static void scanoutReset(unsigned fault=0){assert(scanoutBuffers.empty()&&scanoutPrepares==scanoutCompletes);
 scanoutFault=fault;scanoutAllocCalls=scanoutPrepares=scanoutCompletes=0;}
'''

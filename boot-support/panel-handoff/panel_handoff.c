#include "panel_handoff.h"
#define PANEL_FDT_LIMIT (2u*1024u*1024u)
static uint32_t word(const uint8_t *p,int big)
{
 return big ? (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3] :
     (uint32_t)p[3]<<24|(uint32_t)p[2]<<16|(uint32_t)p[1]<<8|p[0];
}
static int range(size_t off,size_t n,size_t limit) {return off<=limit && n<=limit-off;}
static int exact(const uint8_t *p,size_t n,const char *s)
{size_t i;for(i=0;i<n;++i) if(!s[i] || p[i]!=(uint8_t)s[i]) return 0;return !s[n];}
static int terminated(const uint8_t *p,size_t n,size_t *length)
{size_t i;for(i=0;i<n;++i) if(!p[i]) {*length=i;return 1;}return 0;}
static int space(uint8_t c) {return c==' ' || c=='\t' || c=='\r' || c=='\n';}
static unsigned selection(const uint8_t *args,size_t n,unsigned *profile)
{
 static const char key[]="msm_drm.dsi_display0=";
 size_t pos=0;unsigned count=0,found=0;
 *profile=0;
 if(!n || n>8192 || args[n-1]) return MH_PANEL_MALFORMED;
 for(size_t i=0;i<n-1;++i) if((args[i]<32 && !space(args[i])) || args[i]>126) return MH_PANEL_MALFORMED;
 while(pos<n-1) {
  while(pos<n-1 && space(args[pos])) ++pos;
  size_t begin=pos;while(pos<n-1 && !space(args[pos])) ++pos;
  if(pos-begin<sizeof(key)-1) continue;
  size_t i;for(i=0;i<sizeof(key)-1 && args[begin+i]==(uint8_t)key[i];++i) {}
  if(i!=sizeof(key)-1) continue;
  ++count;size_t value=begin+i,colon=value;
  while(colon<pos && args[colon]!=':') ++colon;
  /* Only the source's default topology/timing. A matching label with an
   * unknown override must not authorize our fixed 1080x2340 pipeline. */
  if(colon==pos || !(exact(args+colon,pos-colon,":") ||
      exact(args+colon,pos-colon,":config0") || exact(args+colon,pos-colon,":config0:timing0"))) continue;
  for(unsigned p=1;p<=4;++p) if(exact(args+value,colon-value,mh2lm_panel_label(p))) found=p;
 }
 if(count>1) return MH_PANEL_AMBIGUOUS;
 if(!count) return MH_PANEL_ABSENT;
 if(!found) return MH_PANEL_UNKNOWN;
 *profile=found;return MH_PANEL_SELECTED;
}
void mh2lm_panel_from_fdt(const void *data,size_t available,uint8_t out[MH2LM_PANEL_IDENTITY_SIZE])
{
 const uint8_t *b=(const uint8_t *)data;unsigned source=MH_PANEL_MALFORMED,profile=0;
 unsigned depth=0,roots=0,chosen=0,bootargs=0;int inChosen=0,ended=0;
 size_t total,st,sz,strings,ssz,pos,end;
 mh2lm_panel_record(out,source,0);
 if(!b || available<40 || word(b,1)!=0xd00dfeedu) return;
 total=word(b+4,1);st=word(b+8,1);strings=word(b+12,1);sz=word(b+36,1);ssz=word(b+32,1);
 if(total<40 || total>available || total>PANEL_FDT_LIMIT || word(b+20,1)!=17 || word(b+24,1)>17 ||
     st<40 || strings<40 || (st&3) || (sz&3) || !range(st,sz,total) || !range(strings,ssz,total) ||
     !(st+sz<=strings || strings+ssz<=st)) return;
 pos=st;end=st+sz;source=MH_PANEL_ABSENT;
 while(range(pos,4,end)) {
  uint32_t token=word(b+pos,1);pos+=4;
  if(token==1) {
   size_t n;if(depth>=64 || !terminated(b+pos,end-pos,&n) || !range(pos,(n+4)&~(size_t)3,end)) goto malformed;
   ++depth;
   if(depth==1) {if(roots++ || n) goto malformed;}
   if(depth==2) {inChosen=exact(b+pos,n,"chosen");if(inChosen) ++chosen;}
   pos+=(n+4)&~(size_t)3;
  } else if(token==2) {
   if(!depth) goto malformed;
   if(depth==2) inChosen=0;
   --depth;
  } else if(token==3) {
   if(!depth || !range(pos,8,end)) goto malformed;
   size_t len=word(b+pos,1),noff=word(b+pos+4,1),name;
   pos+=8;
   if(noff>=ssz || !terminated(b+strings+noff,ssz-noff,&name) ||
       !range(pos,len,end) || !range(pos,(len+3)&~(size_t)3,end)) goto malformed;
   if(depth==2 && inChosen && exact(b+strings+noff,name,"bootargs")) {
    ++bootargs;source=selection(b+pos,len,&profile);
   }
   pos+=(len+3)&~(size_t)3;
  } else if(token==9) {
   if(depth || roots!=1) goto malformed;
   ended=1;break;
  } else if(token!=4) goto malformed;
 }
 if(!ended) goto malformed;
 if(chosen>1 || bootargs>1) {source=MH_PANEL_AMBIGUOUS;profile=0;}
 mh2lm_panel_record(out,source,profile);return;
malformed:
 mh2lm_panel_record(out,MH_PANEL_MALFORMED,0);
}
/* ADT is little-endian; bounded recursion and linear traversal. Metadata has
 * no authority until the shim replaces the single placeholder atomically. */
struct adt_cursor {uint8_t *b;size_t size,pos,slot;unsigned slots,budget;};
static int adt_node(struct adt_cursor *c,unsigned depth,unsigned path)
{
 if(depth>32 || !c->budget-- || !range(c->pos,8,c->size)) return 0;
 unsigned np=word(c->b+c->pos,0),nc=word(c->b+c->pos+4,0);c->pos+=8;
 if(np>(c->size-c->pos)/36 || nc>c->size/8) return 0;
 unsigned nameSeen=0;size_t nameoff=0,namelen=0,slot=0;unsigned slots=0;
 for(unsigned i=0;i<np;++i) {
  if(!range(c->pos,36,c->size)) return 0;
  size_t name,at=c->pos;uint32_t raw=word(c->b+at+32,0);size_t len=raw&0x7fffffffu;
  if(!terminated(c->b+at,32,&name)) return 0;
  c->pos+=36;if(!range(c->pos,(len+3)&~(size_t)3,c->size)) return 0;
  if(exact(c->b+at,name,"name")) {
   ++nameSeen;nameoff=c->pos;namelen=len;
   if(raw>>31 || !len || c->b[c->pos+len-1]) return 0;
  }
  if(exact(c->b+at,name,MH2LM_PANEL_PROPERTY)) {
   if(raw>>31 || len!=MH2LM_PANEL_IDENTITY_SIZE) return 0;
   ++slots;slot=c->pos;
  }
  c->pos+=(len+3)&~(size_t)3;
 }
 if(nameSeen!=1) return 0;
 unsigned next=0;
 if(depth==0) next=1;
 else if(depth==1 && path==1 && exact(c->b+nameoff,namelen-1,"arm-io")) next=2;
 else if(depth==2 && path==2 && exact(c->b+nameoff,namelen-1,"mdss")) next=3;
 if(slots) {
  if(next!=3 || slots!=1) return 0;
  for(size_t i=0;i<MH2LM_PANEL_IDENTITY_SIZE;++i) if(c->b[slot+i]) return 0;
  c->slots+=slots;c->slot=slot;
 }
 for(unsigned i=0;i<nc;++i) if(!adt_node(c,depth+1,next)) return 0;
 return 1;
}
int mh2lm_panel_adt_has_slot(const void *data,size_t size)
{
 struct adt_cursor c={(uint8_t *)(uintptr_t)data,size,0,0,0,4096};
 if(!data || size>2u*1024u*1024u || !adt_node(&c,0,0) || c.pos!=size || c.slots>1) return -1;
 return c.slots ? 1 : 0;
}
int mh2lm_panel_fill_adt(void *data,size_t size,const uint8_t record[MH2LM_PANEL_IDENTITY_SIZE])
{
 struct adt_cursor c={(uint8_t *)data,size,0,0,0,4096};
 if(!record || !mh2lm_panel_source_status(record,MH2LM_PANEL_IDENTITY_SIZE) ||
     !data || size>2u*1024u*1024u || !adt_node(&c,0,0) || c.pos!=size || c.slots>1) return -1;
 if(!c.slots) return 0;
 for(size_t i=0;i<MH2LM_PANEL_IDENTITY_SIZE;++i) c.b[c.slot+i]=record[i];
 return 1;
}

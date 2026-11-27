#include "SweetRPMhExecutor.h"
#if defined(ARM64_BOARD_CONFIG_MH2LM)

SweetRPMhExecutor::Arm SweetRPMhExecutor::arm(Ports &ports,const SweetRPMhLayout &layout,
    uint32_t bit,uint32_t *observed)
{
 const uint32_t status=ports.read(ports.context,layout.irqStatus);
 if (observed) *observed=status;
 if (status==~0u) return ArmInvalid;
 if (status&bit) return ArmStale;
 return ArmReady;
}

SweetRPMhExecutor::Step SweetRPMhExecutor::step(Ports &ports,const SweetRPMhLayout &layout,
    const SweetRPMhWord &word,bool *issued,uint64_t now,uint64_t deadline,uint32_t *observed)
{
 if (!*issued) {
  ports.write(ports.context,word.offset,word.value);
  *issued=true;
 }
 const uint32_t value=ports.read(ports.context,word.w1c ? layout.irqStatus : word.offset);
 if (observed) *observed=value;
 const bool settled=word.w1c ? ((value&word.value)==0) : (value==word.value);
 if (!settled) return now>=deadline ? StepExpired : StepBlocked;
 *issued=false;
 return StepAdvanced;
}

SweetRPMhExecutor::Verdict SweetRPMhExecutor::verify(Ports &ports,const SweetRPMhLayout &layout,
    uint32_t slot,uint32_t bit,unsigned commands,unsigned *unacknowledged)
{
 /* Read order matters more than it looks: the same transaction shape must
  * produce the same bus traffic before and after the extraction, so the whole
  * slot is sampled before any verdict is formed. */
 const uint32_t irq=ports.read(ports.context,layout.irqStatus);
 const uint32_t idle=ports.read(ports.context,slot+layout.status);
 unsigned open=0;
 for (unsigned c=0;c<commands;++c) {
  const uint32_t status=ports.read(ports.context,slot+layout.commandStatus+c*layout.commandStride);
  /* A command that answers ~0 has not acknowledged anything either. */
  if (status==~0u || (status&layout.ackMask)!=layout.ackMask) ++open;
 }
 if (unacknowledged) *unacknowledged=open;
 if (irq==~0u || idle==~0u) return VerdictInvalid;
 if (!idle || !(irq&bit) || open) return VerdictIncomplete;
 return VerdictComplete;
}

bool SweetRPMhExecutor::stageCleanup(Ports &ports,const SweetRPMhLayout &layout,
    uint32_t slot,SweetRPMhWord *words,unsigned *count,uint32_t amc,uint32_t bit,
    uint32_t savedEnable)
{
 unsigned n=*count;
 /* Order matters: the engine stops before its per-slot words are dropped, and
  * the acknowledge lands while the source is still masked. */
 if (n+5>kCapacity) return false;
 words[n++]={slot+layout.control,amc,false};
 words[n++]={slot+layout.control,0,false};
 words[n++]={slot+layout.enable,0,false};
 words[n++]={slot+layout.wait,0,false};
 words[n++]={layout.clear,bit,true};
 const uint32_t enabled=ports.read(ports.context,layout.irqEnable);
 if (enabled==~0u) { *count=n; return false; }
 if (n+1>kCapacity) { *count=n; return false; }
 words[n++]={layout.irqEnable,(enabled&~bit)|(savedEnable&bit),false};
 *count=n;
 return true;
}
#endif
/* Fixed little-endian handoff, not a physical DDIC ID or authentication.
 * Only the allowlisted ABL display choice crosses FDT -> ADT. Never bootargs.
 * Keep identical to XNU SweetPanelIdentity.h (checked by the host test). */
#ifndef MH2LM_PANEL_IDENTITY_H
#define MH2LM_PANEL_IDENTITY_H
#include <stdint.h>
#include <stddef.h>
#define MH2LM_PANEL_IDENTITY_SIZE 96u
#define MH2LM_PANEL_LABEL_SIZE 72u
#define MH2LM_PANEL_PROPERTY "sweet,panel-identity"
enum mh2lm_panel_source { MH_PANEL_UNFILLED, MH_PANEL_ABSENT, MH_PANEL_SELECTED,
 MH_PANEL_UNKNOWN, MH_PANEL_AMBIGUOUS, MH_PANEL_MALFORMED };
static inline const char *mh2lm_panel_label(unsigned profile)
{
 switch (profile) {
 case 1: return "dsi_r66456_tianmamd_pp1_1080_2340_cmd_display";
 case 2: return "dsi_r66456_tianmamd_dv1_1080_2340_cmd_display";
 case 3: return "dsi_r66456_tianmamd_dv2_1080_2340_cmd_display";
 case 4: return "dsi_r66456_tianmamd_pv_1080_2340_cmd_display";
 default: return NULL;
 }
}
static inline void mh2lm_panel_record(uint8_t *out,unsigned source,unsigned profile)
{
 unsigned i;const char *label=source==MH_PANEL_SELECTED ? mh2lm_panel_label(profile) : NULL;
 for (i=0;i<MH2LM_PANEL_IDENTITY_SIZE;++i) out[i]=0;
 for (i=0;i<8;++i) out[i]=(uint8_t)"MHPANEL1"[i];
 out[8]=1;out[12]=(uint8_t)source;
 if (label) {out[16]=(uint8_t)profile;for (i=0;label[i];++i) out[24+i]=(uint8_t)label[i];}
}
/* Exact ABI, zero reserved/tail bytes, label and numeric ID must agree. */
static inline unsigned mh2lm_panel_profile(const uint8_t *record,size_t length)
{
 unsigned i,profile;uint8_t expected[MH2LM_PANEL_IDENTITY_SIZE];
 if (!record || length!=MH2LM_PANEL_IDENTITY_SIZE || record[12]!=MH_PANEL_SELECTED) return 0;
 profile=record[16];if (!mh2lm_panel_label(profile)) return 0;
 mh2lm_panel_record(expected,MH_PANEL_SELECTED,profile);
 for (i=0;i<MH2LM_PANEL_IDENTITY_SIZE;++i) if (record[i]!=expected[i]) return 0;
 return profile;
}
static inline unsigned mh2lm_panel_source_status(const uint8_t *record,size_t length)
{
 unsigned i;uint8_t expected[MH2LM_PANEL_IDENTITY_SIZE];
 if (!record || length!=MH2LM_PANEL_IDENTITY_SIZE || record[12]<MH_PANEL_ABSENT || record[12]>MH_PANEL_MALFORMED) return 0;
 mh2lm_panel_record(expected,record[12],record[16]);
 for (i=0;i<MH2LM_PANEL_IDENTITY_SIZE;++i) if (record[i]!=expected[i]) return 0;
 if (record[12]==MH_PANEL_SELECTED && !mh2lm_panel_profile(record,length)) return 0;
 return record[12];
}
#endif

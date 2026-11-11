/* Board adapter for Mu Silicium's actual ABL x0 HOB. Opt-in ADT slot only.
 * No command-line override or private bootargs copying; no controller I/O.
 * Included by DarwinBoot before kernel/window copies and ExitBootServices. */
#ifndef MH2LM_DARWINBOOT_PANEL_SELECTION_H
#define MH2LM_DARWINBOOT_PANEL_SELECTION_H
#include "panel_handoff.h"
STATIC CONST EFI_GUID mPanelEntryArgHobGuid = {
  0xDAEE82E6, 0x8090, 0x4E72, { 0xBF, 0xB9, 0x78, 0xC1, 0x3C, 0xD4, 0xE3, 0x59 }
};
STATIC BOOLEAN
PanelMemoryRange (CONST EFI_MEMORY_DESCRIPTOR *Map, UINTN MapSize, UINTN DescSize, UINT64 Address, UINTN Size)
{
  if ((DescSize < sizeof (*Map)) || (MapSize % DescSize) || !Size || (Address < 0x80000000ULL)) { return FALSE; }
  for (UINTN Offset = 0; Offset < MapSize; Offset += DescSize) {
    CONST EFI_MEMORY_DESCRIPTOR *D = (CONST EFI_MEMORY_DESCRIPTOR *)((CONST UINT8 *)Map + Offset);
    if ((D->Type > EfiACPIMemoryNVS) || (D->Type == EfiUnusableMemory) ||
        (D->NumberOfPages > (MAX_UINT64 >> EFI_PAGE_SHIFT))) { continue; }
    UINT64 Length = D->NumberOfPages << EFI_PAGE_SHIFT;
    if ((Address >= D->PhysicalStart) && (Address - D->PhysicalStart <= Length) &&
        (Size <= Length - (Address - D->PhysicalStart))) { return TRUE; }
  }
  return FALSE;
}
STATIC EFI_STATUS
PopulatePanelSelection (VOID *Tree, UINTN TreeSize)
{
  UINT8 Record[MH2LM_PANEL_IDENTITY_SIZE];
  EFI_MEMORY_DESCRIPTOR *Map = NULL;
  UINTN MapSize = 0, Key = 0, DescSize = 0;
  UINT32 Version = 0;
  int Slot = mh2lm_panel_adt_has_slot (Tree, TreeSize);
  /* Preserve all legacy ADTs: no HOB lookup or FDT read without the slot. */
  if (Slot == 0) { return EFI_SUCCESS; }
  if (Slot < 0) { return EFI_COMPROMISED_DATA; }
  mh2lm_panel_record (Record, MH_PANEL_ABSENT, 0);
  EFI_HOB_GUID_TYPE *Hob = GetFirstGuidHob (&mPanelEntryArgHobGuid);
  if (Hob != NULL) {
    mh2lm_panel_record (Record, MH_PANEL_MALFORMED, 0);
    if (GET_GUID_HOB_DATA_SIZE (Hob) >= sizeof (UINT64)) {
      UINT64 Address = ReadUnaligned64 ((CONST UINT64 *)GET_GUID_HOB_DATA (Hob));
      EFI_STATUS Status = gBS->GetMemoryMap (&MapSize, NULL, &Key, &DescSize, &Version);
      if ((Status == EFI_BUFFER_TOO_SMALL) && (DescSize >= sizeof (*Map)) &&
          (DescSize <= 4096) && (MapSize <= SIZE_1MB - 8 * DescSize)) {
        MapSize += 8 * DescSize;
        Map = (EFI_MEMORY_DESCRIPTOR *)AllocatePool (MapSize);
        if (Map != NULL) {
          Status = gBS->GetMemoryMap (&MapSize, Map, &Key, &DescSize, &Version);
          if (!EFI_ERROR (Status) && PanelMemoryRange (Map, MapSize, DescSize, Address, 40)) {
            CONST UINT8 *Fdt = (CONST UINT8 *)(UINTN)Address;
            UINTN Size = (UINTN)Fdt[4] << 24 | (UINTN)Fdt[5] << 16 | (UINTN)Fdt[6] << 8 | Fdt[7];
            if ((Size >= 40) && (Size <= SIZE_2MB) && PanelMemoryRange (Map, MapSize, DescSize, Address, Size)) {
              mh2lm_panel_from_fdt (Fdt, Size, Record);
            }
          }
          FreePool (Map);
        }
      }
    }
  }
  /* Invalid/pre-filled slot must not reach XNU as invented source evidence. */
  if (mh2lm_panel_fill_adt (Tree, TreeSize, Record) != 1) { return EFI_COMPROMISED_DATA; }
  DEBUG ((DEBUG_INFO, "DARWINBOOT PANEL: ABL source=%u profile=%u; selection, not physical DDIC ID\n", Record[12], Record[16]));
  return EFI_SUCCESS;
}
#endif

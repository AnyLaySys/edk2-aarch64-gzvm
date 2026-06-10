/** @file

  Copyright (c) 2014-2017, Linaro Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>

RETURN_STATUS
EFIAPI
QemuVirtMemInfoPeiLibConstructor (
  VOID
  )
{
  VOID          *DeviceTreeBase;
  INT32         Node, Prev;
  UINT64        NewBase, CurBase;
  UINT64        NewSize, CurSize;
  CONST CHAR8   *Type;
  INT32         Len;
  CONST UINT64  *RegProp;
  VOID          *Hob;

  NewBase = 0;
  NewSize = 0;

  DeviceTreeBase = (VOID *)(UINTN)PcdGet64 (PcdDeviceTreeInitialBaseAddress);
  ASSERT (DeviceTreeBase != NULL);

  //
  // Make sure we have a valid device tree blob
  //
  ASSERT (FdtCheckHeader (DeviceTreeBase) == 0);

  //
  // Look for the lowest memory node
  //
  for (Prev = 0; ; Prev = Node) {
    Node = FdtNextNode (DeviceTreeBase, Prev, NULL);
    if (Node < 0) {
      break;
    }

    //
    // Check for memory node
    //
    Type = FdtGetProp (DeviceTreeBase, Node, "device_type", &Len);
    if (Type && (AsciiStrnCmp (Type, "memory", Len) == 0)) {
      //
      // Get the 'reg' property of this node. For now, we will assume
      // two 8 byte quantities for base and size, respectively.
      //
      RegProp = FdtGetProp (DeviceTreeBase, Node, "reg", &Len);
      if ((RegProp != 0) && (Len >= (2 * sizeof (UINT64)))) {
        CurBase = Fdt64ToCpu (ReadUnaligned64 (RegProp));
        CurSize = Fdt64ToCpu (ReadUnaligned64 (RegProp + 1));

        DEBUG ((
          DEBUG_INFO,
          "%a: System RAM @ 0x%lx - 0x%lx\n",
          __func__,
          CurBase,
          CurBase + CurSize - 1
          ));

        if ((NewBase > CurBase) || (NewBase == 0)) {
          NewBase = CurBase;
          NewSize = CurSize;
        }
      } else {
        DEBUG ((
          DEBUG_ERROR,
          "%a: Failed to parse FDT memory node\n",
          __func__
          ));
      }
    }
  }

  //
  // Make sure the start of DRAM matches our expectation
  //
  ASSERT (FixedPcdGet64 (PcdSystemMemoryBase) == NewBase);

  //
  // GZVM: subtract the shared swiotlb/restricted-dma-pool region
  // from the reported memory size.  This GUID HOB is used by:
  //   - MemoryPeim to create resource descriptor HOBs
  //   - QemuVirtMemInfoLib to set up the EDK2 MMU virtual memory table
  //
  // The shared DMA pool must NOT be in EDK2's MMU mapping because it may
  // not be accessible at stage-2 during early firmware boot (accessing it
  // causes SIGBUS on the host).  However, it DOES need to appear in the
  // EFI memory map so the Linux kernel creates page table entries for it.
  // MemoryPeim handles this by creating a separate untested resource HOB.
  //
  {
    UINT64       SwiotlbSize;
    INT32        RsvNode;
    INT32        RsvPrev;
    CONST CHAR8  *Compat;
    INT32        CompatLen;
    CONST UINT64 *DmaReg;
    INT32        DmaRegLen;

    SwiotlbSize = 0;

    for (RsvPrev = 0; ; RsvPrev = RsvNode) {
      RsvNode = FdtNextNode (DeviceTreeBase, RsvPrev, NULL);
      if (RsvNode < 0) {
        break;
      }

      Compat = FdtGetProp (DeviceTreeBase, RsvNode, "compatible", &CompatLen);
      if ((Compat != NULL) && (AsciiStrCmp (Compat, "restricted-dma-pool") == 0)) {
        DmaReg = FdtGetProp (DeviceTreeBase, RsvNode, "reg", &DmaRegLen);
        if ((DmaReg != NULL) && (DmaRegLen >= (INT32)(2 * sizeof (UINT64)))) {
          SwiotlbSize = Fdt64ToCpu (ReadUnaligned64 (DmaReg + 1));
        }

        break;
      }
    }

    if ((SwiotlbSize > 0) && (NewSize > SwiotlbSize)) {
      DEBUG ((
        DEBUG_INFO,
        "%a: GZVM: excluding 0x%lx shared DMA from EDK2 allocator (0x%lx -> 0x%lx)\n",
        __func__, SwiotlbSize, NewSize, NewSize - SwiotlbSize
        ));
      NewSize -= SwiotlbSize;
    }
  }

  Hob = BuildGuidDataHob (
          &gArmVirtSystemMemorySizeGuid,
          &NewSize,
          sizeof NewSize
          );
  ASSERT (Hob != NULL);

  //
  // We need to make sure that the machine we are running on has at least
  // 128 MB of memory configured.
  //
  // Note: The FD/DRAM non-overlap assertion is removed for GZVM support.
  // Under GZVM, firmware runs from RAM (PcdFdBaseAddress ==
  // PcdSystemMemoryBase), so the FD intentionally overlaps system memory.
  //
  ASSERT (NewSize >= SIZE_128MB);

  return RETURN_SUCCESS;
}

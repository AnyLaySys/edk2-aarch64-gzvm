/*
 * Copyright (c) 2015, Linaro Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>

/*
 * GZVM: store the restricted-dma-pool shared DMA pool info.
 * Initialized to 1 (not 0) so they land in .data, NOT .bss.
 * BSS is cleared between FindMemnode (assembly) and C runtime,
 * which would zero these if they were in BSS.
 * The sentinel value 1 is overwritten by FindMemnode on success,
 * and checked as "!= 1" won't cause issues (PoolSize is always
 * page-aligned, never 1).
 */
/*
 * Non-zero init forces placement in .data (not .bss).
 * BSS is cleared between FindMemnode() (assembly) and C runtime,
 * which would zero BSS globals. These are set by FindMemnode()
 * when a restricted-dma-pool is found, then read by MemoryPeim
 * and QemuVirtMemInfoLib during C runtime.
 * MARKER value 0xDEAD means "not yet set by FindMemnode".
 */
UINT64  gGzvmSwiotlbBase = 0xDEAD;
UINT64  gGzvmSwiotlbSize = 0xDEAD;

BOOLEAN
FindMemnode (
  IN  VOID    *DeviceTreeBlob,
  OUT UINT64  *SystemMemoryBase,
  OUT UINT64  *SystemMemorySize
  )
{
  INT32        MemoryNode;
  INT32        AddressCells;
  INT32        SizeCells;
  INT32        Length;
  CONST INT32  *Prop;

  if (FdtCheckHeader (DeviceTreeBlob) != 0) {
    return FALSE;
  }

  //
  // Look for a node called "memory" at the lowest level of the tree
  //
  MemoryNode = FdtPathOffset (DeviceTreeBlob, "/memory");
  if (MemoryNode <= 0) {
    return FALSE;
  }

  //
  // Retrieve the #address-cells and #size-cells properties
  // from the root node, or use the default if not provided.
  //
  AddressCells = 1;
  SizeCells    = 1;

  Prop = FdtGetProp (DeviceTreeBlob, 0, "#address-cells", &Length);
  if (Length == 4) {
    AddressCells = Fdt32ToCpu (*Prop);
  }

  Prop = FdtGetProp (DeviceTreeBlob, 0, "#size-cells", &Length);
  if (Length == 4) {
    SizeCells = Fdt32ToCpu (*Prop);
  }

  //
  // Now find the 'reg' property of the /memory node, and read the first
  // range listed.
  //
  Prop = FdtGetProp (DeviceTreeBlob, MemoryNode, "reg", &Length);

  if (Length < (AddressCells + SizeCells) * sizeof (INT32)) {
    return FALSE;
  }

  *SystemMemoryBase = Fdt32ToCpu (Prop[0]);
  if (AddressCells > 1) {
    *SystemMemoryBase = (*SystemMemoryBase << 32) | Fdt32ToCpu (Prop[1]);
  }

  Prop += AddressCells;

  *SystemMemorySize = Fdt32ToCpu (Prop[0]);
  if (SizeCells > 1) {
    *SystemMemorySize = (*SystemMemorySize << 32) | Fdt32ToCpu (Prop[1]);
  }

  //
  // GZVM: check for restricted-dma-pool in /reserved-memory.
  // The shared DMA pool (swiotlb bounce buffer) is at the top of RAM
  // and must be excluded from the UEFI memory map — it's managed by
  // GzvmIoMmuDxe, not by the DXE core allocator.
  //
  {
    INT32        ResvNode;
    INT32        Child;
    CONST CHAR8  *Compat;
    INT32        CompatLen;

    ResvNode = FdtPathOffset (DeviceTreeBlob, "/reserved-memory");
    if (ResvNode >= 0) {
      FdtForEachSubnode (Child, DeviceTreeBlob, ResvNode) {
        Compat = FdtGetProp (DeviceTreeBlob, Child, "compatible", &CompatLen);
        if ((Compat != NULL) &&
            (AsciiStrCmp (Compat, "restricted-dma-pool") == 0))
        {
          CONST INT32 *ResvReg;
          INT32       ResvLen;
          UINT64      PoolSize;

          ResvReg = FdtGetProp (DeviceTreeBlob, Child, "reg", &ResvLen);
          if ((ResvReg != NULL) && (ResvLen >= (AddressCells + SizeCells) * (INT32)sizeof (INT32))) {
            UINT64 PoolBase;
            //
            // Read pool base address
            //
            PoolBase = Fdt32ToCpu (ResvReg[0]);
            if (AddressCells > 1) {
              PoolBase = (PoolBase << 32) | Fdt32ToCpu (ResvReg[1]);
            }

            //
            // Read pool size (skip base address cells)
            //
            PoolSize = Fdt32ToCpu (ResvReg[AddressCells]);
            if (SizeCells > 1) {
              PoolSize = (PoolSize << 32) | Fdt32ToCpu (ResvReg[AddressCells + 1]);
            }

            if (PoolSize > 0 && PoolSize < *SystemMemorySize) {
              *SystemMemorySize -= PoolSize;
              gGzvmSwiotlbBase = PoolBase;
              gGzvmSwiotlbSize = PoolSize;
            }
          }

          break;
        }
      }
    }
  }

  return TRUE;
}

VOID
CopyFdt (
  IN    VOID  *FdtDest,
  IN    VOID  *FdtSource
  )
{
  FdtPack (FdtSource);
  CopyMem (FdtDest, FdtSource, FdtTotalSize (FdtSource));
}

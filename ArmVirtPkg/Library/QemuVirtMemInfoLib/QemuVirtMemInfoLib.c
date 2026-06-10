/** @file

  Copyright (c) 2014-2017, Linaro Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

// Number of Virtual Memory Map Descriptors
#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS  6

//
// mach-virt's core peripherals such as the UART, the GIC and the RTC are
// all mapped in the 'miscellaneous device I/O' region, which we just map
// in its entirety rather than device by device. Note that it does not
// cover any of the NOR flash banks or PCI resource windows.
//
#define MACH_VIRT_PERIPH_BASE  0x08000000
#define MACH_VIRT_PERIPH_SIZE  SIZE_128MB

/**
  Default library constructor that obtains the memory size from a PCD.

  @return  Always returns RETURN_SUCCESS

**/
RETURN_STATUS
EFIAPI
QemuVirtMemInfoLibConstructor (
  VOID
  )
{
  UINT64  Size;
  VOID    *Hob;

  Size = PcdGet64 (PcdSystemMemorySize);
  Hob  = BuildGuidDataHob (&gArmVirtSystemMemorySizeGuid, &Size, sizeof Size);
  ASSERT (Hob != NULL);

  return RETURN_SUCCESS;
}

/**
  Return the Virtual Memory Map of your platform

  This Virtual Memory Map is used by MemoryInitPei Module to initialize the MMU
  on your platform.

  @param[out]   VirtualMemoryMap    Array of ARM_MEMORY_REGION_DESCRIPTOR
                                    describing a Physical-to-Virtual Memory
                                    mapping. This array must be ended by a
                                    zero-filled entry. The allocated memory
                                    will not be freed.

**/
VOID
ArmVirtGetMemoryMap (
  OUT ARM_MEMORY_REGION_DESCRIPTOR  **VirtualMemoryMap
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR  *VirtualMemoryTable;
  VOID                          *MemorySizeHob;

  ASSERT (VirtualMemoryMap != NULL);

  MemorySizeHob = GetFirstGuidHob (&gArmVirtSystemMemorySizeGuid);
  ASSERT (MemorySizeHob != NULL);
  if (MemorySizeHob == NULL) {
    return;
  }

  VirtualMemoryTable = AllocatePool (
                         sizeof (ARM_MEMORY_REGION_DESCRIPTOR) *
                         MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS
                         );

  if (VirtualMemoryTable == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Error: Failed AllocatePool()\n", __func__));
    return;
  }

  // System DRAM: cover full range including the shared swiotlb region.
  // The HOB stores the reduced size. Parse DTB to find the swiotlb pool
  // size and extend the MMU table to cover it.
  {
    UINT64 Extra = 0;
    VOID *Dtb = (VOID *)(UINTN)PcdGet64 (PcdDeviceTreeInitialBaseAddress);
    if (Dtb != NULL && FdtCheckHeader (Dtb) == 0) {
      INT32 Node, Prev;
      for (Prev = 0; ; Prev = Node) {
        Node = FdtNextNode (Dtb, Prev, NULL);
        if (Node < 0) break;
        CONST CHAR8 *C = FdtGetProp (Dtb, Node, "compatible", NULL);
        if (C != NULL && AsciiStrCmp (C, "restricted-dma-pool") == 0) {
          CONST UINT64 *R = FdtGetProp (Dtb, Node, "reg", NULL);
          if (R != NULL) Extra = Fdt64ToCpu (ReadUnaligned64 (&R[1]));
          break;
        }
      }
    }
    VirtualMemoryTable[0].PhysicalBase = PcdGet64 (PcdSystemMemoryBase);
    VirtualMemoryTable[0].VirtualBase  = VirtualMemoryTable[0].PhysicalBase;
    VirtualMemoryTable[0].Length       = *(UINT64 *)GET_GUID_HOB_DATA (MemorySizeHob)
                                         + Extra;
    VirtualMemoryTable[0].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: Dumping System DRAM Memory Map:\n"
    "\tPhysicalBase: 0x%lX\n"
    "\tVirtualBase: 0x%lX\n"
    "\tLength: 0x%lX\n",
    __func__,
    VirtualMemoryTable[0].PhysicalBase,
    VirtualMemoryTable[0].VirtualBase,
    VirtualMemoryTable[0].Length
    ));

  // Memory mapped peripherals (UART, RTC, GIC, virtio-mmio, etc)
  VirtualMemoryTable[1].PhysicalBase = MACH_VIRT_PERIPH_BASE;
  VirtualMemoryTable[1].VirtualBase  = MACH_VIRT_PERIPH_BASE;
  VirtualMemoryTable[1].Length       = MACH_VIRT_PERIPH_SIZE;
  VirtualMemoryTable[1].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  // Map the FV region as normal executable memory
  VirtualMemoryTable[2].PhysicalBase = PcdGet64 (PcdFvBaseAddress);
  VirtualMemoryTable[2].VirtualBase  = VirtualMemoryTable[2].PhysicalBase;
  VirtualMemoryTable[2].Length       = FixedPcdGet32 (PcdFvSize);
  VirtualMemoryTable[2].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_RO;

  // GZVM/PCI peripherals below RAM base (covers PCI MMIO 0x10000000+,
  // PCI ECAM, flash, and other device regions not in MACH_VIRT_PERIPH)
  VirtualMemoryTable[3].PhysicalBase = 0;
  VirtualMemoryTable[3].VirtualBase  = 0;
  VirtualMemoryTable[3].Length       = PcdGet64 (PcdSystemMemoryBase);
  VirtualMemoryTable[3].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  // End of Table
  ZeroMem (&VirtualMemoryTable[4], sizeof (ARM_MEMORY_REGION_DESCRIPTOR));

  *VirtualMemoryMap = VirtualMemoryTable;
}

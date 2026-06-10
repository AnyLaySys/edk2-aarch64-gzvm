/** @file
  GZVM IoMmu DXE driver for DMA bounce buffers.

  In GZVM protected VMs:
    protected guest RAM is not directly accessible to the host
    shared DMA pool memory is accessible to both guest and host

  Both the shared DMA pool base address and size are discovered dynamically
  from the DTB's "restricted-dma-pool" reserved-memory node, which QEMU
  may populate based on the VM memory and static swiotlb configuration.

  PCI virtio devices need DMA buffers the host (QEMU) can read/write.
  This driver installs EDKII_IOMMU_PROTOCOL so PciHostBridgeDxe routes
  all PCI DMA through bounce buffers allocated from the shared DMA pool.

  Copyright (c) 2026, Contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Pi/PiDxeCis.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/IoMmu.h>

//
// Default shared DMA pool size (128 MB) used as fallback when the DTB
// does not contain a "restricted-dma-pool" node.
//
#define GZVM_SHARE_SIZE_DEFAULT  0x08000000ULL   // 128 MB

//
// Runtime-discovered shared DMA pool parameters.
// Set in GzvmIoMmuDxeEntryPoint from the DTB before any allocations.
//
STATIC EFI_PHYSICAL_ADDRESS  mShareBase;
STATIC UINT64                mShareSize;
STATIC UINTN                 mSharePages;

//
// Simple bitmap-based page allocator for the shared DMA pool.
// Each bit represents one 4KB page.  1 = allocated, 0 = free.
// Dynamically allocated based on the actual shared DMA pool size.
//
STATIC UINT8  *mBitmap;

//
// MAP_INFO tracks an active DMA mapping for Unmap().
//
#define GZVM_MAP_INFO_SIGNATURE  SIGNATURE_64 ('G','Z','M','A','P','I','N','F')

typedef struct {
  UINT64                    Signature;
  EDKII_IOMMU_OPERATION     Operation;
  UINTN                     NumberOfBytes;
  UINTN                     NumberOfPages;
  EFI_PHYSICAL_ADDRESS      HostAddress;      // Original protected RAM address
  EFI_PHYSICAL_ADDRESS      BounceAddress;    // Bounce buffer in shared pool
  BOOLEAN                   BounceAllocated;  // TRUE if we allocated the bounce
} GZVM_MAP_INFO;

//
// ============ Pool allocator ============
//

/**
  Allocate contiguous pages from the shared DMA pool.

  @param[in]  Pages  Number of 4KB pages.

  @return  Physical address of allocation, or 0 on failure.
**/
STATIC
EFI_PHYSICAL_ADDRESS
SharePoolAllocatePages (
  IN UINTN  Pages
  )
{
  UINTN  i;
  UINTN  j;

  if ((Pages == 0) || (Pages > mSharePages)) {
    return 0;
  }

  //
  // First-fit search.
  //
  for (i = 0; i <= mSharePages - Pages; i++) {
    BOOLEAN  Found = TRUE;

    for (j = 0; j < Pages; j++) {
      if (mBitmap[(i + j) / 8] & (1U << ((i + j) % 8))) {
        Found = FALSE;
        i    += j;   // skip ahead past this occupied page
        break;
      }
    }

    if (Found) {
      for (j = 0; j < Pages; j++) {
        mBitmap[(i + j) / 8] |= (1U << ((i + j) % 8));
      }

      DEBUG ((
        DEBUG_VERBOSE,
        "GZVM-IoMmu: pool alloc %u pages @ 0x%lx\n",
        (UINT32)Pages,
        mShareBase + i * EFI_PAGE_SIZE
        ));
      return mShareBase + (UINT64)i * EFI_PAGE_SIZE;
    }
  }

  DEBUG ((DEBUG_ERROR, "GZVM-IoMmu: pool exhausted (wanted %u pages)\n", (UINT32)Pages));
  return 0;
}

/**
  Free pages back to the shared DMA pool.

  @param[in]  Address  Physical address returned by SharePoolAllocatePages.
  @param[in]  Pages    Number of pages to free.
**/
STATIC
VOID
SharePoolFreePages (
  IN EFI_PHYSICAL_ADDRESS  Address,
  IN UINTN                 Pages
  )
{
  UINTN  Start;
  UINTN  j;

  if ((Address < mShareBase) ||
      (Address >= mShareBase + mShareSize))
  {
    DEBUG ((DEBUG_ERROR, "GZVM-IoMmu: free 0x%lx is outside shared DMA pool\n", Address));
    return;
  }

  Start = (UINTN)((Address - mShareBase) / EFI_PAGE_SIZE);

  for (j = 0; j < Pages; j++) {
    mBitmap[(Start + j) / 8] &= ~(1U << ((Start + j) % 8));
  }

  DEBUG ((
    DEBUG_VERBOSE,
    "GZVM-IoMmu: pool free  %u pages @ 0x%lx\n",
    (UINT32)Pages,
    Address
    ));
}

//
// ============ EDKII_IOMMU_PROTOCOL implementation ============
//

/**
  Set IOMMU attribute. GZVM does not expose per-device IOMMU attributes here.
**/
STATIC
EFI_STATUS
EFIAPI
GzvmIoMmuSetAttribute (
  IN EDKII_IOMMU_PROTOCOL  *This,
  IN EFI_HANDLE            DeviceHandle,
  IN VOID                  *Mapping,
  IN UINT64                IoMmuAccess
  )
{
  return EFI_SUCCESS;
}

/**
  Map by allocating a bounce buffer in shared DMA pool memory.

  BusMasterRead:         device reads from memory -> copy host->bounce
  BusMasterWrite:        device writes to memory  -> just allocate bounce
  BusMasterCommonBuffer: both sides access same buffer
                         -> HostAddress must already be in the shared pool
**/
STATIC
EFI_STATUS
EFIAPI
GzvmIoMmuMap (
  IN     EDKII_IOMMU_PROTOCOL  *This,
  IN     EDKII_IOMMU_OPERATION  Operation,
  IN     VOID                   *HostAddress,
  IN OUT UINTN                  *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS   *DeviceAddress,
  OUT    VOID                   **Mapping
  )
{
  GZVM_MAP_INFO             *MapInfo;
  EFI_PHYSICAL_ADDRESS     BounceAddr;
  UINTN                    Pages;
  EFI_PHYSICAL_ADDRESS     HostPhys;

  if ((HostAddress == NULL) || (NumberOfBytes == NULL) ||
      (DeviceAddress == NULL) || (Mapping == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  HostPhys = (EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress;

  //
  // If HostAddress is already in the shared pool, no bounce is needed.
  // This happens for CommonBuffer allocations from AllocateBuffer().
  //
  if ((HostPhys >= mShareBase) &&
      (HostPhys < mShareBase + mShareSize))
  {
    MapInfo = AllocateZeroPool (sizeof *MapInfo);
    if (MapInfo == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    MapInfo->Signature      = GZVM_MAP_INFO_SIGNATURE;
    MapInfo->Operation      = Operation;
    MapInfo->NumberOfBytes   = *NumberOfBytes;
    MapInfo->NumberOfPages   = 0;
    MapInfo->HostAddress     = HostPhys;
    MapInfo->BounceAddress   = HostPhys;  // same address, already shared
    MapInfo->BounceAllocated = FALSE;

    *DeviceAddress = HostPhys;
    *Mapping       = MapInfo;

    DEBUG ((
      DEBUG_VERBOSE,
      "GZVM-IoMmu: Map shared host=0x%lx dev=0x%lx bytes=%u\n",
      HostPhys,
      *DeviceAddress,
      (UINT32)*NumberOfBytes
      ));
    return EFI_SUCCESS;
  }

  //
  // HostAddress is in protected RAM and needs a shared bounce buffer.
  //
  Pages     = EFI_SIZE_TO_PAGES (*NumberOfBytes);
  BounceAddr = SharePoolAllocatePages (Pages);
  if (BounceAddr == 0) {
    return EFI_OUT_OF_RESOURCES;
  }

  MapInfo = AllocateZeroPool (sizeof *MapInfo);
  if (MapInfo == NULL) {
    SharePoolFreePages (BounceAddr, Pages);
    return EFI_OUT_OF_RESOURCES;
  }

  MapInfo->Signature      = GZVM_MAP_INFO_SIGNATURE;
  MapInfo->Operation      = Operation;
  MapInfo->NumberOfBytes   = *NumberOfBytes;
  MapInfo->NumberOfPages   = Pages;
  MapInfo->HostAddress     = HostPhys;
  MapInfo->BounceAddress   = BounceAddr;
  MapInfo->BounceAllocated = TRUE;

  //
  // For BusMasterRead (device reads from memory), copy data to bounce now.
  // For BusMasterWrite (device writes to memory), zero the bounce buffer.
  // For CommonBuffer, copy data so device sees current state.
  //
  switch (Operation) {
    case EdkiiIoMmuOperationBusMasterRead:
    case EdkiiIoMmuOperationBusMasterRead64:
      CopyMem ((VOID *)(UINTN)BounceAddr, HostAddress, *NumberOfBytes);
      break;

    case EdkiiIoMmuOperationBusMasterWrite:
    case EdkiiIoMmuOperationBusMasterWrite64:
      ZeroMem ((VOID *)(UINTN)BounceAddr, EFI_PAGES_TO_SIZE (Pages));
      break;

    case EdkiiIoMmuOperationBusMasterCommonBuffer:
    case EdkiiIoMmuOperationBusMasterCommonBuffer64:
      CopyMem ((VOID *)(UINTN)BounceAddr, HostAddress, *NumberOfBytes);
      break;

    default:
      SharePoolFreePages (BounceAddr, Pages);
      FreePool (MapInfo);
      return EFI_INVALID_PARAMETER;
  }

  *DeviceAddress = BounceAddr;
  *Mapping       = MapInfo;

  DEBUG ((
    DEBUG_VERBOSE,
    "GZVM-IoMmu: Map op=%d host=0x%lx -> bounce=0x%lx bytes=%u pages=%u\n",
    (INT32)Operation,
    HostPhys,
    BounceAddr,
    (UINT32)*NumberOfBytes,
    (UINT32)Pages
    ));

  return EFI_SUCCESS;
}

/**
  Unmap: for device writes, copy bounce data back and free the bounce buffer.
**/
STATIC
EFI_STATUS
EFIAPI
GzvmIoMmuUnmap (
  IN EDKII_IOMMU_PROTOCOL  *This,
  IN VOID                   *Mapping
  )
{
  GZVM_MAP_INFO  *MapInfo;

  if (Mapping == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MapInfo = (GZVM_MAP_INFO *)Mapping;
  if (MapInfo->Signature != GZVM_MAP_INFO_SIGNATURE) {
    return EFI_INVALID_PARAMETER;
  }

  if (MapInfo->BounceAllocated) {
    //
    // Copy data back from bounce for write & common-buffer operations.
    //
    switch (MapInfo->Operation) {
      case EdkiiIoMmuOperationBusMasterWrite:
      case EdkiiIoMmuOperationBusMasterWrite64:
      case EdkiiIoMmuOperationBusMasterCommonBuffer:
      case EdkiiIoMmuOperationBusMasterCommonBuffer64:
        CopyMem (
          (VOID *)(UINTN)MapInfo->HostAddress,
          (VOID *)(UINTN)MapInfo->BounceAddress,
          MapInfo->NumberOfBytes
          );
        break;

      default:
        break;
    }

    SharePoolFreePages (MapInfo->BounceAddress, MapInfo->NumberOfPages);

    DEBUG ((
      DEBUG_VERBOSE,
      "GZVM-IoMmu: Unmap bounce=0x%lx -> host=0x%lx bytes=%u\n",
      MapInfo->BounceAddress,
      MapInfo->HostAddress,
      (UINT32)MapInfo->NumberOfBytes
      ));
  }

  ZeroMem (MapInfo, sizeof *MapInfo);
  FreePool (MapInfo);
  return EFI_SUCCESS;
}

/**
  AllocateBuffer from the shared DMA pool.
**/
STATIC
EFI_STATUS
EFIAPI
GzvmIoMmuAllocateBuffer (
  IN     EDKII_IOMMU_PROTOCOL  *This,
  IN     EFI_ALLOCATE_TYPE      Type,
  IN     EFI_MEMORY_TYPE        MemoryType,
  IN     UINTN                  Pages,
  IN OUT VOID                   **HostAddress,
  IN     UINT64                 Attributes
  )
{
  EFI_PHYSICAL_ADDRESS  Addr;

  if (HostAddress == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Addr = SharePoolAllocatePages (Pages);
  if (Addr == 0) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem ((VOID *)(UINTN)Addr, EFI_PAGES_TO_SIZE (Pages));
  *HostAddress = (VOID *)(UINTN)Addr;

  DEBUG ((
    DEBUG_VERBOSE,
    "GZVM-IoMmu: AllocateBuffer %u pages @ 0x%lx\n",
    (UINT32)Pages,
    Addr
    ));

  return EFI_SUCCESS;
}

/**
  FreeBuffer returns pages to the shared DMA pool.
**/
STATIC
EFI_STATUS
EFIAPI
GzvmIoMmuFreeBuffer (
  IN EDKII_IOMMU_PROTOCOL  *This,
  IN UINTN                  Pages,
  IN VOID                   *HostAddress
  )
{
  if (HostAddress == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  SharePoolFreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress, Pages);
  return EFI_SUCCESS;
}

//
// Protocol instance
//
STATIC EDKII_IOMMU_PROTOCOL  mGzvmIoMmu = {
  EDKII_IOMMU_PROTOCOL_REVISION,
  GzvmIoMmuSetAttribute,
  GzvmIoMmuMap,
  GzvmIoMmuUnmap,
  GzvmIoMmuAllocateBuffer,
  GzvmIoMmuFreeBuffer,
};

/**
  Entry point: install the IOMMU protocol.
**/
EFI_STATUS
EFIAPI
GzvmIoMmuDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           Handle;
  UINTN                BitmapBytes;
  VOID                 *DeviceTreeBase;
  INT32                Node;
  INT32                Prev;

  //
  // Discover the shared DMA pool from the DTB's "restricted-dma-pool" node.
  // We parse the DTB directly (like the PEI constructor) instead of using
  // FdtClient protocol. This allows [Depex] TRUE so we load BEFORE
  // HighMemDxe and claim the shared DMA pool first.
  //
  mShareBase  = 0;
  mShareSize  = 0;

  //
  // Get the DTB from the HOB (same source FdtClientDxe uses).
  // The PEI phase copies the DTB and stores its address in gFdtHobGuid.
  //
  {
    VOID  *Hob;
    Hob = GetFirstGuidHob (&gFdtHobGuid);
    if ((Hob != NULL) && (GET_GUID_HOB_DATA_SIZE (Hob) == sizeof (UINT64))) {
      DeviceTreeBase = (VOID *)(UINTN)*(UINT64 *)GET_GUID_HOB_DATA (Hob);
    } else {
      DeviceTreeBase = NULL;
    }
  }

  if ((DeviceTreeBase != NULL) && (FdtCheckHeader (DeviceTreeBase) == 0)) {
    for (Prev = 0; ; Prev = Node) {
      Node = FdtNextNode (DeviceTreeBase, Prev, NULL);
      if (Node < 0) {
        break;
      }

      {
        CONST CHAR8   *Compat;
        INT32          CompatLen;
        CONST UINT64  *DmaReg;
        INT32          DmaRegLen;

        Compat = FdtGetProp (DeviceTreeBase, Node, "compatible", &CompatLen);
        if ((Compat != NULL) && (AsciiStrCmp (Compat, "restricted-dma-pool") == 0)) {
          DmaReg = FdtGetProp (DeviceTreeBase, Node, "reg", &DmaRegLen);
          if ((DmaReg != NULL) && (DmaRegLen >= (INT32)(2 * sizeof (UINT64)))) {
            mShareBase = Fdt64ToCpu (ReadUnaligned64 (DmaReg));
            mShareSize = Fdt64ToCpu (ReadUnaligned64 (DmaReg + 1));
            DEBUG ((
              DEBUG_INFO,
              "GZVM-IoMmu: DTB restricted-dma-pool @ 0x%lx size 0x%lx\n",
              (UINT64)mShareBase,
              (UINT64)mShareSize
              ));
          }

          break;
        }
      }
    }
  }

  if ((mShareBase == 0) || (mShareSize == 0)) {
    DEBUG ((DEBUG_WARN, "GZVM-IoMmu: restricted-dma-pool not found, using PCD fallback\n"));
    mShareBase  = PcdGet64 (PcdSystemMemoryBase) + PcdGet64 (PcdSystemMemorySize);
    mShareSize  = GZVM_SHARE_SIZE_DEFAULT;
  }

  mSharePages = (UINTN)(mShareSize / EFI_PAGE_SIZE);

  //
  // Allocate the bitmap for the page allocator.
  // Each bit represents one 4KB page.
  //
  BitmapBytes = (mSharePages + 7) / 8;
  mBitmap = AllocateZeroPool (BitmapBytes);
  if (mBitmap == NULL) {
    DEBUG ((DEBUG_ERROR, "GZVM-IoMmu: failed to allocate bitmap (%u bytes)\n", (UINT32)BitmapBytes));
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((
    DEBUG_INFO,
    "GZVM-IoMmu: shared DMA pool at 0x%lx, size 0x%lx (%u pages)\n",
    (UINT64)mShareBase,
    (UINT64)mShareSize,
    (UINT32)mSharePages
    ));

  //
  // Add the shared DMA pool to GCD and set page table attributes.
  // With [Depex] TRUE we load before HighMemDxe, so this should succeed.
  //
  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeSystemMemory,
                  mShareBase,
                  mShareSize,
                  EFI_MEMORY_WB | EFI_MEMORY_XP
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "GZVM-IoMmu: AddMemorySpace failed: %r (may already exist)\n", Status));
  }

  Status = gDS->SetMemorySpaceAttributes (
                  mShareBase,
                  mShareSize,
                  EFI_MEMORY_WB | EFI_MEMORY_XP
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "GZVM-IoMmu: SetMemorySpaceAttributes failed: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "GZVM-IoMmu: page tables created for shared DMA pool\n"));

  //
  // Mark the shared DMA pool as Reserved so the general-purpose page
  // allocator never hands out pages from it. Only GzvmIoMmuDxe's
  // internal bitmap allocator should use this pool for DMA bounce buffers.
  //
  {
    EFI_PHYSICAL_ADDRESS  ResvAddr = mShareBase;
    Status = gBS->AllocatePages (
                    AllocateAddress,
                    EfiReservedMemoryType,
                    mSharePages,
                    &ResvAddr
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "GZVM-IoMmu: failed to reserve shared DMA pool: %r\n", Status));
    } else {
      DEBUG ((DEBUG_INFO, "GZVM-IoMmu: shared DMA pool reserved (%u pages)\n",
              (UINT32)mSharePages));
    }
  }

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEdkiiIoMmuProtocolGuid,
                  &mGzvmIoMmu,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "GZVM-IoMmu: install protocol failed: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "GZVM-IoMmu: EDKII_IOMMU_PROTOCOL installed\n"));
  return EFI_SUCCESS;
}

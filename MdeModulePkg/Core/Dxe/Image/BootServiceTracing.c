/** @file
  Boot service call tracing for debugging 3rd-party EFI applications.
  Wraps key boot services with logging to diagnose hangs.
**/

#include "DxeMain.h"

STATIC UINTN                mHeartbeatCount = 0;

STATIC
VOID
EFIAPI
TracingHeartbeat (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mHeartbeatCount++;
  DEBUG ((DEBUG_ERROR, "TRACE: HEARTBEAT #%u (timer IRQ working)\n",
          (UINT32)mHeartbeatCount));
}

STATIC EFI_ALLOCATE_PAGES   mOrigAllocatePages;
STATIC EFI_LOCATE_PROTOCOL  mOrigLocateProtocol;
STATIC EFI_CREATE_EVENT     mOrigCreateEvent;
STATIC EFI_SET_TIMER        mOrigSetTimer;
STATIC EFI_GET_MEMORY_MAP   mOrigGetMemoryMap;
STATIC EFI_EXIT_BOOT_SERVICES mOrigExitBootServices;
STATIC EFI_HANDLE_PROTOCOL  mOrigHandleProtocol;
STATIC EFI_OPEN_PROTOCOL    mOrigOpenProtocol;
STATIC EFI_RAISE_TPL        mOrigRaiseTPL;
STATIC EFI_RESTORE_TPL      mOrigRestoreTPL;
STATIC UINTN                mTraceCallCount = 0;
STATIC BOOLEAN              mVerbosePhase = FALSE;  // Enable after GetMemoryMap

STATIC
EFI_TPL
EFIAPI
TracingRaiseTPL (
  IN EFI_TPL  NewTpl
  )
{
  EFI_TPL OldTpl;
  OldTpl = mOrigRaiseTPL (NewTpl);
  DEBUG ((DEBUG_ERROR, "T[%u]:RaiseTPL(%u->%u)\n",
          (UINT32)mTraceCallCount++, (UINT32)OldTpl, (UINT32)NewTpl));
  return OldTpl;
}

STATIC
VOID
EFIAPI
TracingRestoreTPL (
  IN EFI_TPL  OldTpl
  )
{
  DEBUG ((DEBUG_ERROR, "T[%u]:RestoreTPL(%u)\n",
          (UINT32)mTraceCallCount++, (UINT32)OldTpl));
  mOrigRestoreTPL (OldTpl);
}

STATIC
EFI_STATUS
EFIAPI
TracingHandleProtocol (
  IN  EFI_HANDLE  Handle,
  IN  EFI_GUID    *Protocol,
  OUT VOID        **Interface
  )
{
  EFI_STATUS Status;
  Status = mOrigHandleProtocol (Handle, Protocol, Interface);
  if (mVerbosePhase) {
    DEBUG ((DEBUG_ERROR, "T[%u]:HP(%g)=%r\n",
            (UINT32)mTraceCallCount++, Protocol, Status));
  }
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TracingOpenProtocol (
  IN  EFI_HANDLE  Handle,
  IN  EFI_GUID    *Protocol,
  OUT VOID        **Interface OPTIONAL,
  IN  EFI_HANDLE  AgentHandle,
  IN  EFI_HANDLE  ControllerHandle,
  IN  UINT32      Attributes
  )
{
  EFI_STATUS Status;
  Status = mOrigOpenProtocol (Handle, Protocol, Interface, AgentHandle, ControllerHandle, Attributes);
  if (mVerbosePhase) {
    DEBUG ((DEBUG_ERROR, "T[%u]:OP(%g)=%r\n",
            (UINT32)mTraceCallCount++, Protocol, Status));
  }
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TracingAllocatePages (
  IN     EFI_ALLOCATE_TYPE     Type,
  IN     EFI_MEMORY_TYPE       MemoryType,
  IN     UINTN                 Pages,
  IN OUT EFI_PHYSICAL_ADDRESS  *Memory
  )
{
  EFI_STATUS Status;
  Status = mOrigAllocatePages (Type, MemoryType, Pages, Memory);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "TRACE[%u]: AllocatePages(type=%d,mtype=%d,pages=%u,addr=0x%lx) = %r\n",
            (UINT32)mTraceCallCount++, (INT32)Type, (INT32)MemoryType,
            (UINT32)Pages, (Memory ? *Memory : 0), Status));
  }
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TracingLocateProtocol (
  IN  EFI_GUID  *Protocol,
  IN  VOID      *Registration OPTIONAL,
  OUT VOID      **Interface
  )
{
  EFI_STATUS Status;
  Status = mOrigLocateProtocol (Protocol, Registration, Interface);
  DEBUG ((DEBUG_INFO, "TRACE[%u]: LocateProtocol(%g) = %r\n",
          (UINT32)mTraceCallCount++, Protocol, Status));
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TracingCreateEvent (
  IN  UINT32            Type,
  IN  EFI_TPL           NotifyTpl,
  IN  EFI_EVENT_NOTIFY  NotifyFunction OPTIONAL,
  IN  VOID              *NotifyContext OPTIONAL,
  OUT EFI_EVENT         *Event
  )
{
  EFI_STATUS Status;
  Status = mOrigCreateEvent (Type, NotifyTpl, NotifyFunction, NotifyContext, Event);
  DEBUG ((DEBUG_INFO, "TRACE[%u]: CreateEvent(type=0x%x,tpl=%u,fn=0x%lx) = %r\n",
          (UINT32)mTraceCallCount++, Type, (UINT32)NotifyTpl,
          (UINT64)(UINTN)NotifyFunction, Status));
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TracingSetTimer (
  IN EFI_EVENT        Event,
  IN EFI_TIMER_DELAY  Type,
  IN UINT64           TriggerTime
  )
{
  EFI_STATUS Status;
  Status = mOrigSetTimer (Event, Type, TriggerTime);
  DEBUG ((DEBUG_INFO, "TRACE[%u]: SetTimer(type=%d,time=%lu) = %r\n",
          (UINT32)mTraceCallCount++, (INT32)Type, TriggerTime, Status));
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TracingGetMemoryMap (
  IN OUT UINTN                  *MemoryMapSize,
  IN OUT EFI_MEMORY_DESCRIPTOR  *MemoryMap,
  OUT    UINTN                  *MapKey,
  OUT    UINTN                  *DescriptorSize,
  OUT    UINT32                 *DescriptorVersion
  )
{
  EFI_STATUS Status;
  Status = mOrigGetMemoryMap (MemoryMapSize, MemoryMap, MapKey, DescriptorSize, DescriptorVersion);
  {
    // Check ARM64 interrupt state (DAIF register) and current EL
    UINT64 Daif, CurrentEl;
    __asm__ volatile ("mrs %0, daif" : "=r" (Daif));
    __asm__ volatile ("mrs %0, currentel" : "=r" (CurrentEl));
    DEBUG ((DEBUG_ERROR, "TRACE[%u]: GetMemoryMap(size=%u) = %r [DAIF=0x%lx EL=%u]\n",
            (UINT32)mTraceCallCount++, (UINT32)(MemoryMapSize ? *MemoryMapSize : 0), Status,
            Daif, (UINT32)((CurrentEl >> 2) & 3)));
    if (!EFI_ERROR (Status)) {
      mVerbosePhase = TRUE;
      DEBUG ((DEBUG_ERROR, "TRACE: Verbose phase ON — logging HandleProtocol/OpenProtocol\n"));
    }
  }

  //
  // HACK: Remove the phantom MMIO entry at address 0x0 (from NOR flash
  // GCD mapping).  Under GZVM the flash isn't accessible and this entry
  // breaks memory map sort order, potentially causing Windows cdboot to
  // spin in an infinite loop.
  //
  if (!EFI_ERROR (Status) && (MemoryMap != NULL) && (DescriptorSize != NULL) &&
      (MemoryMapSize != NULL))
  {
    EFI_MEMORY_DESCRIPTOR  *Entry;
    UINTN                  Count;
    UINTN                  i;

    Count = *MemoryMapSize / *DescriptorSize;
    for (i = 0; i < Count; i++) {
      Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)MemoryMap + i * (*DescriptorSize));
      if ((Entry->Type == EfiMemoryMappedIO) && (Entry->PhysicalStart == 0)) {
        DEBUG ((DEBUG_WARN, "TRACE: Removing phantom MMIO entry at 0x0 (index %u)\n", (UINT32)i));
        // Shift remaining entries down
        UINTN Remaining = (Count - i - 1) * (*DescriptorSize);
        if (Remaining > 0) {
          CopyMem (
            (UINT8 *)MemoryMap + i * (*DescriptorSize),
            (UINT8 *)MemoryMap + (i + 1) * (*DescriptorSize),
            Remaining
            );
        }
        *MemoryMapSize -= *DescriptorSize;
        Count--;
        break;
      }
    }
  }

  //
  // Dump the memory map on successful calls for GZVM diagnostics.
  //
  if (!EFI_ERROR (Status) && (MemoryMap != NULL) && (DescriptorSize != NULL)) {
    EFI_MEMORY_DESCRIPTOR  *Entry;
    UINTN                  Count;
    UINTN                  i;

    Count = *MemoryMapSize / *DescriptorSize;
    DEBUG ((DEBUG_ERROR, "TRACE: MemoryMap dump (%u entries, descSize=%u):\n",
            (UINT32)Count, (UINT32)*DescriptorSize));
    for (i = 0; i < Count; i++) {
      Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)MemoryMap + i * (*DescriptorSize));
      DEBUG ((DEBUG_ERROR, "  [%02u] type=%02u phys=0x%09lx-0x%09lx pages=%06lx attr=0x%lx\n",
              (UINT32)i, (UINT32)Entry->Type,
              Entry->PhysicalStart,
              Entry->PhysicalStart + (Entry->NumberOfPages << 12) - 1,
              Entry->NumberOfPages,
              Entry->Attribute));
    }
    DEBUG ((DEBUG_ERROR, "TRACE: MemoryMap dump end\n"));
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TracingExitBootServices (
  IN EFI_HANDLE  ImageHandle,
  IN UINTN       MapKey
  )
{
  DEBUG ((DEBUG_ERROR, "TRACE[%u]: ExitBootServices(MapKey=0x%lx) CALLED!\n",
          (UINT32)mTraceCallCount++, (UINT64)MapKey));
  return mOrigExitBootServices (ImageHandle, MapKey);
}

VOID
InstallBootServiceTracing (
  EFI_BOOT_SERVICES  *BS
  )
{
  DEBUG ((DEBUG_ERROR, "TRACE: Installing boot service tracing hooks\n"));

  //
  // Dump EFI System Table ConfigurationTable — shows what ACPI/FDT
  // entries cdboot.efi will find when it reads the system table.
  //
  {
    EFI_SYSTEM_TABLE *ST = gST;
    UINTN i;
    DEBUG ((DEBUG_ERROR, "TRACE: ConfigurationTable (%u entries):\n",
            (UINT32)ST->NumberOfTableEntries));
    for (i = 0; i < ST->NumberOfTableEntries; i++) {
      DEBUG ((DEBUG_ERROR, "  [%u] GUID=%g Addr=0x%lx\n",
              (UINT32)i,
              &ST->ConfigurationTable[i].VendorGuid,
              (UINT64)(UINTN)ST->ConfigurationTable[i].VendorTable));
    }
  }

  //
  // Override the synchronous exception vector to print diagnostics
  // directly to PL011 (bypassing DEBUG macro TPL restrictions).
  // On ARM64, cdboot may crash and the default handler's output
  // gets suppressed.
  //
  {
    // Read current VBAR_EL1 to know where exceptions go
    UINT64 Vbar;
    __asm__ volatile ("mrs %0, vbar_el1" : "=r" (Vbar));
    DEBUG ((DEBUG_ERROR, "TRACE: VBAR_EL1=0x%lx (exception vectors)\n", Vbar));
  }

  //
  // Install a periodic heartbeat timer that fires every ~2 seconds.
  // This confirms the UEFI timer interrupt is still working while
  // cdboot is "hung".
  //
  {
    EFI_EVENT  HeartbeatEvent;
    EFI_STATUS HbStatus;

    HbStatus = gBS->CreateEvent (
                      EVT_TIMER | EVT_NOTIFY_SIGNAL,
                      TPL_CALLBACK,
                      TracingHeartbeat,
                      NULL,
                      &HeartbeatEvent
                      );
    if (!EFI_ERROR (HbStatus)) {
      gBS->SetTimer (HeartbeatEvent, TimerPeriodic, 20000000); // 2 seconds
      DEBUG ((DEBUG_ERROR, "TRACE: Heartbeat timer installed (2s interval)\n"));
    }
  }

  //
  // HACK: Remove EFI_MEMORY_ATTRIBUTES_TABLE from config table.
  // GZVM keeps this table out of the config table. Windows cdboot
  // may process it and crash if NX attributes are unexpected.
  //
  {
    EFI_GUID MemAttrGuid = { 0x4C19049F, 0x4137, 0x4DD3,
      { 0x9C, 0x10, 0x8B, 0x97, 0xA8, 0x3F, 0xFD, 0xFA } };
    EFI_STATUS MatStatus;
    MatStatus = gBS->InstallConfigurationTable (&MemAttrGuid, NULL);
    DEBUG ((DEBUG_ERROR, "TRACE: Removed MemoryAttributesTable: %r\n", MatStatus));
  }

  mOrigAllocatePages    = BS->AllocatePages;
  mOrigLocateProtocol   = BS->LocateProtocol;
  mOrigCreateEvent      = BS->CreateEvent;
  mOrigSetTimer         = BS->SetTimer;
  mOrigGetMemoryMap     = BS->GetMemoryMap;
  mOrigExitBootServices = BS->ExitBootServices;
  mOrigHandleProtocol   = BS->HandleProtocol;
  mOrigOpenProtocol     = BS->OpenProtocol;
  mOrigRaiseTPL         = BS->RaiseTPL;
  mOrigRestoreTPL       = BS->RestoreTPL;

  BS->RaiseTPL         = TracingRaiseTPL;
  BS->RestoreTPL       = TracingRestoreTPL;
  BS->AllocatePages    = TracingAllocatePages;
  BS->LocateProtocol   = TracingLocateProtocol;
  BS->CreateEvent      = TracingCreateEvent;
  BS->SetTimer         = TracingSetTimer;
  BS->GetMemoryMap     = TracingGetMemoryMap;
  BS->ExitBootServices = TracingExitBootServices;
  BS->HandleProtocol   = TracingHandleProtocol;
  BS->OpenProtocol     = TracingOpenProtocol;

  //
  // Recalculate CRC32 since we modified the table
  //
  BS->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 ((UINT8 *)BS, BS->Hdr.HeaderSize, &BS->Hdr.CRC32);
}

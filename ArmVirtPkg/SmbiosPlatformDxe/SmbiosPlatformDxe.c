/** @file
 *
 *  Static SMBIOS Table for the GZVM Platform
 *  Adapted for the GZVM QEMU virt platform.
 *
 *  Copyright (c) 2017-2018, Andrey Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) 2013, Linaro.org
 *  Copyright (c) 2012, Apple Inc. All rights reserved.<BR>
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Copyright (c) 2020, ARM Limited. All rights reserved.
 *  Copyright (c) 2021, BigfootACA <bigfoot@classfun.cn>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Guid/SmBios.h>
#include <IndustryStandard/SmBios.h>
#include <Library/ArmLib.h>
#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/BaseLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/FdtClient.h>
#include <Protocol/Smbios.h>

extern EFI_GUID gArmVirtSystemMemorySizeGuid;

/***********************************************************************
        SMBIOS data definition  TYPE0  BIOS Information
************************************************************************/
SMBIOS_TABLE_TYPE0 mBIOSInfoType0 = {
    {EFI_SMBIOS_TYPE_BIOS_INFORMATION, sizeof(SMBIOS_TABLE_TYPE0), 0},
    1,      // Vendor String
    2,      // BiosVersion String
    0,      // BiosSegment
    3,      // BiosReleaseDate String
    0x3F,   // BiosSize (in 64KB) = 4 MiB
    {
        // BiosCharacteristics
        0, //  Reserved                          :2;
        0, //  Unknown                           :1;
        0, //  BiosCharacteristicsNotSupported   :1;
        0, //  IsaIsSupported                    :1;
        0, //  McaIsSupported                    :1;
        0, //  EisaIsSupported                   :1;
        1, //  PciIsSupported                    :1;
        0, //  PcmciaIsSupported                 :1;
        1, //  PlugAndPlayIsSupported            :1;
        0, //  ApmIsSupported                    :1;
        1, //  BiosIsUpgradable                  :1;
        0, //  BiosShadowingAllowed              :1;
        0, //  VlVesaIsSupported                 :1;
        0, //  EscdSupportIsAvailable            :1;
        0, //  BootFromCdIsSupported             :1;
        1, //  SelectableBootIsSupported         :1;
        0, //  RomBiosIsSocketed                 :1;
        0, //  BootFromPcmciaIsSupported         :1;
        0, //  EDDSpecificationIsSupported       :1;
        0, //  JapaneseNecFloppyIsSupported      :1;
        0, //  JapaneseToshibaFloppyIsSupported  :1;
        0, //  Floppy525_360IsSupported          :1;
        0, //  Floppy525_12IsSupported           :1;
        0, //  Floppy35_720IsSupported           :1;
        0, //  Floppy35_288IsSupported           :1;
        0, //  PrintScreenIsSupported            :1;
        0, //  Keyboard8042IsSupported           :1;
        1, //  SerialIsSupported                 :1;
        0, //  PrinterIsSupported                :1;
        0, //  CgaMonoIsSupported                :1;
        0, //  NecPc98                           :1;
        0  //  ReservedForVendor                 :32;
    },
    {
        // BIOSCharacteristicsExtensionBytes[]
        0x03, //  AcpiIsSupported                   :1;
              //  UsbLegacyIsSupported              :1;
        0x0C, //  BiosBootSpecIsSupported              :1;
              //  FunctionKeyNetworkBootIsSupported    :1;
              //  TargetContentDistributionEnabled     :1;
              //  UefiSpecificationSupported           :1;
              //  VirtualMachineSupported              :1;
    },
    0,    // SystemBiosMajorRelease
    0,    // SystemBiosMinorRelease
    0xFF, // EmbeddedControllerFirmwareMajorRelease
    0xFF, // EmbeddedControllerFirmwareMinorRelease
};

CHAR8 mBiosVendor[128]  = "QEMU-GZVM";
CHAR8 mBiosVersion[128] = "edk2-gzvm";
CHAR8 mBiosDate[12]     = __DATE__;

CHAR8 *mBIOSInfoType0Strings[] = {
    mBiosVendor,  // Vendor
    mBiosVersion, // Version
    mBiosDate,    // Release Date
    NULL};

/***********************************************************************
        SMBIOS data definition  TYPE1  System Information
************************************************************************/
SMBIOS_TABLE_TYPE1 mSysInfoType1 = {
    {EFI_SMBIOS_TYPE_SYSTEM_INFORMATION, sizeof(SMBIOS_TABLE_TYPE1), 0},
    1, // Manufacturer String
    2, // ProductName String
    3, // Version String
    4, // SerialNumber String
    {0x078D22D5, 0x8B22, 0x54EE, { 0x0A, 0x29, 0x41, 0x65, 0xD2, 0xFB, 0xEE, 0xC4}},
    SystemWakeupTypePowerSwitch,
    5, // SKUNumber String
    6, // Family String
};

CHAR8 mSysInfoManufName[128]                 = "Qualcomm";
CHAR8 mSysInfoProductName[128]               = "GZVM Virtual Machine";
CHAR8 mSysInfoVersionName[128]               = "0.1.0";
CHAR8 mSysInfoSerial[sizeof(UINT64) * 2 + 1] = "Serial Not Set";
CHAR8 mSysInfoSKU[sizeof(UINT64) * 2 + 1]    = "SKU Not Set";

CHAR8 *mSysInfoType1Strings[] = {
    mSysInfoManufName,
    mSysInfoProductName,
    mSysInfoVersionName,
    mSysInfoSerial,
    mSysInfoSKU,
    "GZVM Virtual Machine",
    NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE2  Board Information
************************************************************************/
SMBIOS_TABLE_TYPE2 mBoardInfoType2 = {
    {EFI_SMBIOS_TYPE_BASEBOARD_INFORMATION, sizeof(SMBIOS_TABLE_TYPE2), 0},
    1, // Manufacturer String
    2, // ProductName String
    3, // Version String
    4, // SerialNumber String
    5, // AssetTag String
    {
        // FeatureFlag
        1, //  Motherboard           :1;
        0, //  RequiresDaughterCard  :1;
        0, //  Removable             :1;
        0, //  Replaceable           :1;
        0, //  HotSwappable          :1;
        0, //  Reserved              :3;
    },
    6,                        // LocationInChassis String
    0,                        // ChassisHandle;
    BaseBoardTypeMotherBoard, // BoardType;
    0,                        // NumberOfContainedObjectHandles;
    {0}                       // ContainedObjectHandles[1];
};

CHAR8 mChassisAssetTag[128];

CHAR8 *mBoardInfoType2Strings[] = {
    mSysInfoManufName,
    mSysInfoProductName,
    mSysInfoVersionName,
    mSysInfoSerial,
    mChassisAssetTag,
    "Virtual Machine",
    NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE3  Enclosure Information
************************************************************************/
SMBIOS_TABLE_TYPE3 mEnclosureInfoType3 = {
    {EFI_SMBIOS_TYPE_SYSTEM_ENCLOSURE, sizeof(SMBIOS_TABLE_TYPE3), 0},
    1,                         // Manufacturer String
    MiscChassisTypeDeskTop,    // Type;
    2,                         // Version String
    3,                         // SerialNumber String
    4,                         // AssetTag String
    ChassisStateSafe,          // BootupState;
    ChassisStateSafe,          // PowerSupplyState;
    ChassisStateSafe,          // ThermalState;
    ChassisSecurityStatusNone, // SecurityStatus;
    {0, 0, 0, 0},              // OemDefined[4];
    1,                         // Height;
    1,                         // NumberofPowerCords;
    0,                         // ContainedElementCount;
    0,                         // ContainedElementRecordLength;
    {{0}},                     // ContainedElements[1];
};
CHAR8 *mEnclosureInfoType3Strings[] = {
    mSysInfoManufName, mSysInfoProductName, mSysInfoSerial, mChassisAssetTag,
    NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE4  Processor Information
************************************************************************/
SMBIOS_TABLE_TYPE4 mProcessorInfoType4 = {
    {EFI_SMBIOS_TYPE_PROCESSOR_INFORMATION, sizeof(SMBIOS_TABLE_TYPE4), 0},
    1,                // Socket String
    CentralProcessor, // ProcessorType;
    ProcessorFamilyIndicatorFamily2, // ProcessorFamily;
    2,                               // ProcessorManufacture String;
    {                                // ProcessorId;
     0x00, 0x00, 0x00, 0x00,
    },
    3, // ProcessorVersion String;
    {
        // Voltage;
        0, // ProcessorVoltageCapability5V        :1;
        0, // ProcessorVoltageCapability3_3V      :1;
        0, // ProcessorVoltageCapability2_9V      :1;
        0, // ProcessorVoltageCapabilityReserved  :1;
        0, // ProcessorVoltageReserved            :3;
        1  // ProcessorVoltageIndicateLegacy      :1;
    },
    0,                     // ExternalClock;
    2000,                  // MaxSpeed;
    2000,                  // CurrentSpeed;
    0x41,                  // Status;
    ProcessorUpgradeOther, // ProcessorUpgrade;
    0xFFFF,                // L1CacheHandle;
    0xFFFF,                // L2CacheHandle;
    0xFFFF,                // L3CacheHandle;
    0,                     // SerialNumber;
    0,                     // AssetTag;
    0,                     // PartNumber;
    1,                     // CoreCount;
    1,                     // EnabledCoreCount;
    0,                     // ThreadCount;
    0xEC, // ProcessorCharacteristics;
    ProcessorFamilyARMv8, // ARM Processor Family;
    0,                    // CoreCount2;
    0,                    // EnabledCoreCount2;
    0,                    // ThreadCount2;
};

CHAR8 *mProcessorInfoType4Strings[] = {
    "BUILTIN", "Qualcomm", "GZVM vCPU", NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE 11  OEM Strings
************************************************************************/
SMBIOS_TABLE_TYPE11 mOemStringsType11 = {
    {EFI_SMBIOS_TYPE_OEM_STRINGS, sizeof(SMBIOS_TABLE_TYPE11), 0},
    1 // StringCount
};
CHAR8 *mOemStringsType11Strings[] = {
    "QEMU GZVM Virtual Machine", NULL
};

/***********************************************************************
        SMBIOS data definition  TYPE16  Physical Memory Array Information
************************************************************************/
SMBIOS_TABLE_TYPE16 mPhyMemArrayInfoType16 = {
    {EFI_SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY, sizeof(SMBIOS_TABLE_TYPE16), 0},
    MemoryArrayLocationSystemBoard, // Location;
    MemoryArrayUseSystemMemory,     // Use;
    MemoryErrorCorrectionNone,      // MemoryErrorCorrection;
    0,                              // MaximumCapacity; (in KB) updated at runtime from DTB
    0xFFFE,                         // MemoryErrorInformationHandle;
    1,                              // NumberOfMemoryDevices;
    0x00000000ULL,                  // ExtendedMaximumCapacity;
};
CHAR8 *mPhyMemArrayInfoType16Strings[] = {NULL};

/***********************************************************************
        SMBIOS data definition  TYPE17  Memory Device Information
************************************************************************/
SMBIOS_TABLE_TYPE17 mMemDevInfoType17 = {
    {EFI_SMBIOS_TYPE_MEMORY_DEVICE, sizeof(SMBIOS_TABLE_TYPE17), 0},
    0,      // MemoryArrayHandle;
    0xFFFE, // MemoryErrorInformationHandle;
    64,     // TotalWidth;
    64,     // DataWidth;
    0,      // Size; in MB — updated at runtime from DTB
    MemoryFormFactorOther,      // FormFactor;
    0,                          // DeviceSet;
    1,                          // DeviceLocator String
    2,                          // BankLocator String
    MemoryTypeOther,  // MemoryType;
    {
        // TypeDetail;
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, // Unbuffered
        0,
    },
    1866,                 // Speed;
    2,                    // Manufacturer String
    0,                    // SerialNumber String
    0,                    // AssetTag String
    0,                    // PartNumber String
    0,                    // Attributes;
    0,                    // ExtendedSize;
    0,                    // ConfiguredMemoryClockSpeed;
    0,                    // MinimumVoltage;
    0,                    // MaximumVoltage;
    0,                    // ConfiguredVoltage;
    MemoryTechnologyDram, // MemoryTechnology
    {{
        // MemoryOperatingModeCapability
        0, 0, 0,
        1, // VolatileMemory
        0, 0, 0
    }},
    0, 0, 0, 0, 0,
    0,                     // NonVolatileSize
    0xFFFFFFFFFFFFFFFFULL, // VolatileSize
    0,                     // CacheSize
    0,                     // LogicalSize
    0,                     // ExtendedSpeed,
    0                      // ExtendedConfiguredMemorySpeed
};
CHAR8 *mMemDevInfoType17Strings[] = {"Builtin", "BANK 0", NULL};

/***********************************************************************
        SMBIOS data definition  TYPE19  Memory Array Mapped Address Information
************************************************************************/
SMBIOS_TABLE_TYPE19 mMemArrMapInfoType19 = {
    {EFI_SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS, sizeof(SMBIOS_TABLE_TYPE19),
     0},
    0xFFFFFFFF, // StartingAddress;
    0xFFFFFFFF, // EndingAddress;
    0, // MemoryArrayHandle;
    1, // PartitionWidth;
    0, // ExtendedStartingAddress;
    0, // ExtendedEndingAddress;
};
CHAR8 *mMemArrMapInfoType19Strings[] = {NULL};

/***********************************************************************
        SMBIOS data definition  TYPE32  Boot Information
************************************************************************/
SMBIOS_TABLE_TYPE32 mBootInfoType32 = {
    {EFI_SMBIOS_TYPE_SYSTEM_BOOT_INFORMATION, sizeof(SMBIOS_TABLE_TYPE32), 0},
    {0, 0, 0, 0, 0, 0},          // Reserved[6];
    BootInformationStatusNoError // BootStatus
};
CHAR8 *mBootInfoType32Strings[] = {NULL};

/**
   Create SMBIOS record.
**/
STATIC
EFI_STATUS
LogSmbiosData(
  IN EFI_SMBIOS_TABLE_HEADER *Template,
  IN CHAR8 **StringPack,
  OUT EFI_SMBIOS_HANDLE *DataSmbiosHandle
  )
{
  EFI_STATUS               Status;
  EFI_SMBIOS_PROTOCOL *    Smbios;
  EFI_SMBIOS_HANDLE        SmbiosHandle;
  EFI_SMBIOS_TABLE_HEADER *Record;
  UINTN                    Index;
  UINTN                    StringSize;
  UINTN                    Size;
  CHAR8 *                  Str;

  Status = gBS->LocateProtocol(&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Size = Template->Length;
  if (StringPack == NULL) {
    Size += 2;
  }
  else {
    for (Index = 0; StringPack[Index] != NULL; Index++) {
      StringSize = AsciiStrSize(StringPack[Index]);
      Size += StringSize;
    }
    if (StringPack[0] == NULL) {
      Size += 1;
    }
    Size += 1;
  }

  Record = (EFI_SMBIOS_TABLE_HEADER *)AllocateZeroPool(Size);
  if (Record == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem(Record, Template, Template->Length);

  Str = ((CHAR8 *)Record) + Record->Length;
  if (StringPack != NULL) {
    for (Index = 0; StringPack[Index] != NULL; Index++) {
      StringSize = AsciiStrSize(StringPack[Index]);
      CopyMem(Str, StringPack[Index], StringSize);
      Str += StringSize;
    }
  }

  *Str         = 0;
  SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  Status       = Smbios->Add(Smbios, gImageHandle, &SmbiosHandle, Record);

  if ((Status == EFI_SUCCESS) && (DataSmbiosHandle != NULL)) {
    *DataSmbiosHandle = SmbiosHandle;
  }

  ASSERT_EFI_ERROR(Status);
  FreePool(Record);
  return Status;
}

/**
  Estimate the CPU frequency in MHz using the PMU cycle counter.
**/
STATIC
UINT32
EstimateCpuFrequencyMHz (
  VOID
  )
{
  UINT64  StartTicks, EndTicks, WaitTicks;
  UINT64  StartCycles, EndCycles;
  UINT64  DeltaTicks, DeltaCycles;
  UINT64  PmcrVal, FreqMHz, Dfr0, CntFreq;

  CntFreq = ArmGenericTimerGetTimerFreq ();
  if (CntFreq == 0) {
    return 0;
  }

  asm volatile ("mrs %0, id_aa64dfr0_el1" : "=r" (Dfr0));
  if (((Dfr0 >> 8) & 0xFULL) == 0) {
    DEBUG ((DEBUG_INFO, "%a: PMU not available, skipping CPU frequency estimation\n", __func__));
    return 0;
  }

  asm volatile ("mrs %0, pmcr_el0" : "=r" (PmcrVal));
  PmcrVal |= (1ULL << 0) | (1ULL << 2);
  asm volatile ("msr pmcr_el0, %0" : : "r" (PmcrVal));
  asm volatile ("msr pmcntenset_el0, %0" : : "r" (1ULL << 31));
  asm volatile ("isb");

  WaitTicks = DivU64x32 (CntFreq, 100);

  asm volatile ("mrs %0, pmccntr_el0" : "=r" (StartCycles));
  StartTicks = ArmGenericTimerGetSystemCount ();

  do {
    EndTicks = ArmGenericTimerGetSystemCount ();
  } while ((EndTicks - StartTicks) < WaitTicks);

  asm volatile ("mrs %0, pmccntr_el0" : "=r" (EndCycles));

  DeltaTicks  = EndTicks  - StartTicks;
  DeltaCycles = EndCycles - StartCycles;

  if (DeltaTicks == 0 || DeltaCycles == 0) {
    return 0;
  }

  FreqMHz = DivU64x64Remainder (
              MultU64x64 (DeltaCycles, CntFreq),
              MultU64x64 (DeltaTicks, 1000000ULL),
              NULL
              );

  if (FreqMHz < 100 || FreqMHz > 10000) {
    return 0;
  }

  return (UINT32)FreqMHz;
}

STATIC
VOID
LogCpuInfo(
  IN FDT_CLIENT_PROTOCOL *FdtClient
  )
{
  EFI_STATUS Status;
  INT32 Node;
  UINT32 Count = 0;
  UINT32 FreqMHz;

  Status = FdtClient->FindCompatibleNode (
    FdtClient, "arm,armv8", &Node
  );
  while (!EFI_ERROR (Status)) {
    Count ++;
    Status = FdtClient->FindNextCompatibleNode (
      FdtClient, "arm,armv8", Node, &Node
    );
  }

  if (Count != 0) {
    DEBUG((DEBUG_INFO, "%a: Found %u CPU nodes\n", __func__, Count));
    mProcessorInfoType4.CoreCount = Count;
    mProcessorInfoType4.EnabledCoreCount = Count;
  }

  FreqMHz = EstimateCpuFrequencyMHz ();
  if (FreqMHz != 0) {
    DEBUG ((DEBUG_INFO, "%a: Estimated CPU frequency: %u MHz\n", __func__, FreqMHz));
    mProcessorInfoType4.CurrentSpeed = (UINT16)FreqMHz;
    mProcessorInfoType4.MaxSpeed     = (UINT16)FreqMHz;
  }

  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mProcessorInfoType4,
      mProcessorInfoType4Strings, NULL);
}

STATIC
VOID
LogMemoryInfo(
  IN FDT_CLIENT_PROTOCOL *FdtClient
  )
{
  EFI_STATUS Status;
  INT32 Node;
  CONST UINT32 *Reg;
  UINT32 RegSize;
  UINTN AddressCells, SizeCells;
  UINT64 CurBase, CurSize;

  Status = FdtClient->FindMemoryNodeReg (
    FdtClient, &Node, (CONST VOID **)&Reg,
    &AddressCells, &SizeCells, &RegSize
  );
  while (!EFI_ERROR (Status)) {
    ASSERT (AddressCells <= 2);
    ASSERT (SizeCells <= 2);

    while (RegSize > 0) {
      CurBase = SwapBytes32 (*Reg++);
      if (AddressCells > 1)
        CurBase = (CurBase << 32) | SwapBytes32 (*Reg++);
      CurSize = SwapBytes32 (*Reg++);
      if (SizeCells > 1)
        CurSize = (CurSize << 32) | SwapBytes32 (*Reg++);
      RegSize -= (AddressCells + SizeCells) * sizeof (UINT32);

      DEBUG ((DEBUG_INFO,
        "%a: Found memory region: 0x%llx - 0x%llx (length: 0x%llx)\n",
        __func__, CurBase, CurBase + CurSize - 1, CurSize
      ));

      mMemArrMapInfoType19.StartingAddress = RShiftU64(CurBase, 10);
      mMemArrMapInfoType19.EndingAddress = RShiftU64(CurBase + CurSize - 1, 10);
      LogSmbiosData(
          (EFI_SMBIOS_TABLE_HEADER *)&mMemArrMapInfoType19,
          mMemArrMapInfoType19Strings, NULL);
    }

    Status = FdtClient->FindNextMemoryNodeReg (
      FdtClient, Node, &Node, (CONST VOID **)&Reg,
      &AddressCells, &SizeCells, &RegSize
    );
  }

  // Note: TYPE16/TYPE17 size fields already set by UpdateMemorySizeFromFdt()
  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mMemDevInfoType17, mMemDevInfoType17Strings,
      NULL);
}

/**
  Read DTB memory nodes to compute total RAM size, then update
  TYPE16 MaximumCapacity and TYPE17 Size/VolatileSize before they
  are logged.
**/
STATIC
VOID
UpdateMemorySize (
  VOID
  )
{
  UINT64       TotalSize;
  UINT64       TotalSizeInMB;
  VOID         *GuidHob;
  UINT64       *HobData;

  //
  // PcdSystemMemorySize is NOT updated at runtime (stays at DSC default).
  // The real RAM size is in the gArmVirtSystemMemorySizeGuid HOB built
  // by QemuVirtMemInfoPeiLibConstructor.
  //
  TotalSize = 0;
  GuidHob = GetFirstGuidHob (&gArmVirtSystemMemorySizeGuid);
  if (GuidHob != NULL) {
    HobData = GET_GUID_HOB_DATA (GuidHob);
    TotalSize = *HobData;
  }

  DEBUG ((DEBUG_INFO, "%a: RAM size from HOB = 0x%lx\n", __func__, TotalSize));

  if (TotalSize == 0) {
    return;
  }

  TotalSizeInMB = RShiftU64 (TotalSize, 20);

  // TYPE16: MaximumCapacity in KB
  mPhyMemArrayInfoType16.MaximumCapacity = (UINT32)RShiftU64 (TotalSize, 10);

  // TYPE17: Size in MB (or ExtendedSize if >= 32 GB)
  if (TotalSizeInMB >= 0x7FFF) {
    mMemDevInfoType17.Size = 0x7FFF;
    mMemDevInfoType17.ExtendedSize = (UINT32)TotalSizeInMB;
  } else {
    mMemDevInfoType17.Size = (UINT16)TotalSizeInMB;
  }
  mMemDevInfoType17.VolatileSize = TotalSize;

  DEBUG ((DEBUG_INFO, "%a: Total RAM from DTB: %lu MB\n",
    __func__, TotalSizeInMB));
}

EFI_STATUS
EFIAPI
SmbiosPlatformDriverEntryPoint(
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_SMBIOS_HANDLE SmbiosHandle;
  FDT_CLIENT_PROTOCOL *FdtClient;

  Status = gBS->LocateProtocol (
                  &gFdtClientProtocolGuid,
                  NULL,
                  (VOID **)&FdtClient
                  );
  ASSERT_EFI_ERROR (Status);

  // TYPE0 BIOS Information
  AsciiSPrint(
      mBiosVersion, sizeof(mBiosVersion), "edk2-gzvm %s",
      (CHAR16 *)FixedPcdGetPtr(PcdFirmwareVersionString));
  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mBIOSInfoType0, mBIOSInfoType0Strings, NULL);

  // TYPE1 System Information
  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mSysInfoType1, mSysInfoType1Strings, NULL);

  // TYPE3 Enclosure Information
  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mEnclosureInfoType3,
      mEnclosureInfoType3Strings, &SmbiosHandle);
  mBoardInfoType2.ChassisHandle = (UINT16)SmbiosHandle;

  // TYPE2 Board Information
  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mBoardInfoType2, mBoardInfoType2Strings,
      NULL);

  // TYPE11 OEM Strings
  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mOemStringsType11, mOemStringsType11Strings,
      NULL);

  // Set TYPE16/17 memory sizes from PCD before logging
  UpdateMemorySize();

  // TYPE16 Physical Memory Array Information (MaximumCapacity now set from DTB)
  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mPhyMemArrayInfoType16,
      mPhyMemArrayInfoType16Strings, &SmbiosHandle);
  mMemArrMapInfoType19.MemoryArrayHandle = SmbiosHandle;
  mMemDevInfoType17.MemoryArrayHandle    = SmbiosHandle;

  LogCpuInfo(FdtClient);
  LogMemoryInfo(FdtClient);

  // TYPE32 Boot Information
  LogSmbiosData(
      (EFI_SMBIOS_TABLE_HEADER *)&mBootInfoType32, mBootInfoType32Strings,
      NULL);

  return EFI_SUCCESS;
}

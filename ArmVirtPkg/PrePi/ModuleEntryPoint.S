//
//  Copyright (c) 2011-2013, ARM Limited. All rights reserved.
//  Copyright (c) 2015-2016, Linaro Limited. All rights reserved.
//
//  SPDX-License-Identifier: BSD-2-Clause-Patent
//
//

#include <AsmMacroLib.h>

ASM_FUNC(_ModuleEntryPoint)
  //
  // Set VBAR_EL1 to a minimal exception vector table FIRST, before any
  // code that could cause exceptions.  Under GZVM, the default VBAR
  // (0x0) points to MMIO trap space which causes a fatal page fault
  // if any exception occurs during early init.
  //
  adr   x9, _EarlyExceptionVectors
  msr   vbar_el1, x9
  isb

  //
  // Diagnostic breadcrumbs: write letters to PL011 to trace execution
  //
  mov   x15, #0x09000000    // PL011 base (kept in x15 for reuse; QEMU virt fixed address)

  mov   w16, #'a'           // 'a' = VBAR set, starting DTB check
  str   w16, [x15]

  //
  // If we are booting from RAM using the Linux kernel boot protocol, x0 will
  // point to the DTB image in memory. Otherwise, use the default value defined
  // by the platform.
  //
  cbnz  x0, 0f
  LDR_LIT(x0, PcdGet64 (PcdDeviceTreeInitialBaseAddress))

0:mov   x28, x0             // preserve DTB pointer
  mov   x27, x1             // preserve base of image pointer

  mov   w16, #'b'           // 'b' = before ArmEnableVFP
  str   w16, [x15]
  // Print x27 (image base) top 3 nibbles
  mov   w16, #'='
  str   w16, [x15]
  mov   x17, x27
  lsr   x17, x17, #28
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  10f
  add   w16, w16, #('A'-'0'-10)
10:str  w16, [x15]
  mov   x17, x27
  lsr   x17, x17, #24
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  11f
  add   w16, w16, #('A'-'0'-10)
11:str  w16, [x15]
  mov   x17, x27
  lsr   x17, x17, #20
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  12f
  add   w16, w16, #('A'-'0'-10)
12:str  w16, [x15]
  mov   w16, #' '
  str   w16, [x15]

  // Enable Floating Point. This needs to be done before entering C code, which
  // may use FP/SIMD registers.
  mov   x19, x15            // save UART base (x19 callee-saved, preserved by C)
  bl    ArmEnableVFP
  mov   x15, x19            // restore UART base

  // Print DTB pointer as hex nibbles to diagnose bad address
  mov   w16, #'c'
  str   w16, [x15]
  mov   w16, #'='
  str   w16, [x15]
  // Print x28 (DTB pointer) as 8 hex digits (top 32 bits)
  mov   x17, x28
  lsr   x17, x17, #28
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  1f
  add   w16, w16, #('A'-'0'-10)
1:str   w16, [x15]
  mov   x17, x28
  lsr   x17, x17, #24
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  2f
  add   w16, w16, #('A'-'0'-10)
2:str   w16, [x15]
  mov   x17, x28
  lsr   x17, x17, #20
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  3f
  add   w16, w16, #('A'-'0'-10)
3:str   w16, [x15]
  mov   w16, #' '
  str   w16, [x15]

  bl    ASM_PFX(DiscoverDramFromDt)

  mov   w16, #'d'           // 'd' = after DiscoverDramFromDt
  str   w16, [x15]

  // Get ID of this CPU in Multicore system
  mov   x19, x15            // save UART base
  bl    ASM_PFX(ArmReadMpidr)
  mov   x15, x19            // restore UART base
  // Keep a copy of the MpId register value
  mov   x20, x0

// Check if we can install the stack at the top of the System Memory or if we need
// to install the stacks at the bottom of the Firmware Device (case the FD is located
// at the top of the DRAM)
_SetupStackPosition:
  // Compute Top of System Memory
  LDR_LIT(x1, PcdGet64 (PcdSystemMemoryBase))
  LDR_LIT(x2, PcdGet64 (PcdSystemMemorySize))
  sub   x2, x2, #1
  add   x1, x1, x2      // x1 = SystemMemoryTop = PcdSystemMemoryBase + PcdSystemMemorySize

  // Calculate Top of the Firmware Device
  LDR_LIT(x2, PcdGet64 (PcdFdBaseAddress))
  MOV32 (w3, FixedPcdGet32 (PcdFdSize) - 1)
  add   x3, x3, x2      // x3 = FdTop = PcdFdBaseAddress + PcdFdSize

  // UEFI Memory Size (stacks are allocated in this region)
  MOV32 (x4, FixedPcdGet32(PcdSystemMemoryUefiRegionSize))

  //
  // Reserve the memory for the UEFI region (contain stacks on its top)
  //

  // Calculate how much space there is between the top of the Firmware and the Top of the System Memory
  subs  x0, x1, x3   // x0 = SystemMemoryTop - FdTop
  b.mi  _SetupStack  // Jump if negative (FdTop > SystemMemoryTop). Case when the PrePi is in XIP memory outside of the DRAM
  cmp   x0, x4
  b.ge  _SetupStack

  // Case the top of stacks is the FdBaseAddress
  mov   x1, x2

_SetupStack:
  // x1 contains the top of the stack (and the UEFI Memory)

  // Because the 'push' instruction is equivalent to 'stmdb' (decrement before), we need to increment
  // one to the top of the stack. We check if incrementing one does not overflow (case of DRAM at the
  // top of the memory space)
  adds  x21, x1, #1
  b.cs  _SetupOverflowStack

_SetupAlignedStack:
  mov   x1, x21
  b     _GetBaseUefiMemory

_SetupOverflowStack:
  // Case memory at the top of the address space. Ensure the top of the stack is EFI_PAGE_SIZE
  // aligned (4KB)
  and   x1, x1, ~EFI_PAGE_MASK

_GetBaseUefiMemory:
  // Calculate the Base of the UEFI Memory
  sub   x21, x1, x4

_GetStackBase:
  // r1 = The top of the Mpcore Stacks
  mov   sp, x1

  // Stack for the primary core = PrimaryCoreStack
  MOV32 (x2, FixedPcdGet32(PcdCPUCorePrimaryStackSize))
  sub   x22, x1, x2

  mov   x0, x20
  mov   x1, x21
  mov   x2, x22

  // Set the frame pointer to NULL so any backtraces terminate here
  mov   x29, xzr

  // Jump to PrePiCore C code
  //    x0 = MpId
  //    x1 = UefiMemoryBase
  //    x2 = StacksBase
  mov   x19, x15            // save UART base
  bl    ASM_PFX(CEntryPoint)
  mov   x15, x19            // restore UART base

_NeverReturn:
  b _NeverReturn

//
// Minimal early exception vector table for PrePi.
// Each vector is a simple deadloop — just enough to avoid faulting on
// unmapped memory at address 0x0 (GZVM MMIO trap space).
// The real exception handlers are installed later by CpuDxe.
//
// ARM64 requires 2KB alignment for the vector table.
// Each handler writes a unique character to PL011 UART (0x9000000)
// before deadlooping, so we can identify which exception fired.
//
.macro EXCEPTION_ENTRY char
  mov   x10, #0x09000000
  mov   w11, #\char
  str   w11, [x10]
  mov   w11, #'\n'
  str   w11, [x10]
0:wfe
  b     0b
.endm

.balign 2048
_EarlyExceptionVectors:
  // Current EL with SP0
  EXCEPTION_ENTRY '0'    // 0x000: Synchronous
  .balign 128
  EXCEPTION_ENTRY '1'    // 0x080: IRQ
  .balign 128
  EXCEPTION_ENTRY '2'    // 0x100: FIQ
  .balign 128
  EXCEPTION_ENTRY '3'    // 0x180: SError
  .balign 128
  // Current EL with SPx
  EXCEPTION_ENTRY '4'    // 0x200: Synchronous
  .balign 128
  EXCEPTION_ENTRY '5'    // 0x280: IRQ
  .balign 128
  EXCEPTION_ENTRY '6'    // 0x300: FIQ
  .balign 128
  EXCEPTION_ENTRY '7'    // 0x380: SError
  .balign 128
  // Lower EL using AArch64
  EXCEPTION_ENTRY '8'    // 0x400: Synchronous
  .balign 128
  EXCEPTION_ENTRY '9'    // 0x480: IRQ
  .balign 128
  EXCEPTION_ENTRY 'A'    // 0x500: FIQ
  .balign 128
  EXCEPTION_ENTRY 'B'    // 0x580: SError
  .balign 128
  // Lower EL using AArch32
  EXCEPTION_ENTRY 'C'    // 0x600: Synchronous
  .balign 128
  EXCEPTION_ENTRY 'D'    // 0x680: IRQ
  .balign 128
  EXCEPTION_ENTRY 'E'    // 0x700: FIQ
  .balign 128
  EXCEPTION_ENTRY 'F'    // 0x780: SError

// VOID
// DiscoverDramFromDt (
//   VOID   *DeviceTreeBaseAddress,   // passed by loader in x0, preserved in x28
//   VOID   *ImageBase                // passed by FDF trampoline in x1, preserved in x27
//   );
ASM_PFX(DiscoverDramFromDt):
  mov   x29, x30            // preserve LR

  mov   w16, #'1'
  str   w16, [x15]

  //
  // The base of the runtime image has been preserved in x27. Check whether
  // the expected magic number can be found in the header.
  //
  LDR_LIT_TMP(w8, .LArm64LinuxMagic, x8)

  mov   w16, #'2'
  str   w16, [x15]

  ldr   w9, [x27, #0x38]

  mov   w16, #'3'
  str   w16, [x15]

  cmp   w8, w9
  bne   .Lout

  mov   w16, #'4'           // not exception '4', this is breadcrumb '4'
  str   w16, [x15]

  //
  // OK, so far so good. We have confirmed that we likely have a DTB and are
  // booting via the arm64 Linux boot protocol. Update the base-of-image PCD
  // to the actual relocated value, and add the shift of PcdFdBaseAddress to
  // PcdFvBaseAddress as well
  //
  adr   x8, PcdGet64 (PcdFdBaseAddress)
  adr   x9, PcdGet64 (PcdFvBaseAddress)

  mov   w16, #'5'
  str   w16, [x15]

  ldr   x6, [x8]
  ldr   x7, [x9]
  sub   x7, x7, x6
  add   x7, x7, x27
  str   x27, [x8]
  str   x7, [x9]

  mov   w16, #'6'
  str   w16, [x15]

  //
  // The runtime address may be different from the link time address so fix
  // up the PE/COFF relocations. Since we are calling a C function, we need
  // a temp stack.  The original code uses "the window at the beginning of
  // the FD image" — but on GZVM the FD base == RAM base (0x80000000),
  // so the stack would grow into unmapped MMIO trap space below.
  // Instead, use a 4 KB temp stack just past the FD image end.
  //
  mov   x0, x7
  adr   x1, PeCoffLoaderImageReadFromMemory
  add   x10, x27, #0x800000  // temp stack = FD base + 8 MB (well past DTB at +4MB)
  mov   sp, x10
  mov   x19, x15            // save UART base
  bl    RelocatePeCoffImage
  mov   x15, x19            // restore UART base

  //
  // Discover the memory size and offset from the DTB, and record in the
  // respective PCDs. This will also return false if a corrupt DTB is
  // encountered.
  //
  mov   x0, x28
  adr   x1, PcdGet64 (PcdSystemMemoryBase)
  adr   x2, PcdGet64 (PcdSystemMemorySize)
  mov   x19, x15            // save UART base
  bl    FindMemnode
  mov   x15, x19            // restore UART base
  cbz   x0, .Lout

  //
  // Copy the DTB to the slack space right after the 64 byte arm64/Linux style
  // image header at the base of this image (defined in the FDF), and record the
  // pointer in PcdDeviceTreeInitialBaseAddress.
  //
  // The slack space is 1 MB minus the 64 byte header = 0xFFFFC0 bytes.
  // Check the DTB size before copying to avoid overflowing into FVMAIN_COMPACT.
  //
  adr   x8, PcdGet64 (PcdDeviceTreeInitialBaseAddress)
  add   x27, x27, #0x40        // dest = FD base + 0x40 (after header)
  ldr   w10, [x28, #4]         // FDT totalsize (be32)
  rev   w10, w10
  //
  // Print DTB size as hex for diagnostics
  //
  mov   w16, #'s'
  str   w16, [x15]
  mov   w16, #'='
  str   w16, [x15]
  mov   x17, x10
  lsr   x17, x17, #28
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  11f
  add   w16, w16, #('A'-'0'-10)
 11:str w16, [x15]
  mov   x17, x10
  lsr   x17, x17, #24
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  12f
  add   w16, w16, #('A'-'0'-10)
 12:str w16, [x15]
  mov   x17, x10
  lsr   x17, x17, #20
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  13f
  add   w16, w16, #('A'-'0'-10)
 13:str w16, [x15]
  mov   x17, x10
  lsr   x17, x17, #16
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  14f
  add   w16, w16, #('A'-'0'-10)
 14:str w16, [x15]
  mov   x17, x10
  lsr   x17, x17, #12
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  15f
  add   w16, w16, #('A'-'0'-10)
 15:str w16, [x15]
  mov   x17, x10
  lsr   x17, x17, #8
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  16f
  add   w16, w16, #('A'-'0'-10)
 16:str w16, [x15]
  mov   x17, x10
  lsr   x17, x17, #4
  and   w16, w17, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  17f
  add   w16, w16, #('A'-'0'-10)
 17:str w16, [x15]
  and   w16, w10, #0xF
  add   w16, w16, #'0'
  cmp   w16, #('9'+1)
  b.lt  18f
  add   w16, w16, #('A'-'0'-10)
 18:str w16, [x15]
  mov   w16, #' '
  str   w16, [x15]

  mov   w11, #0xFFC0
  movk  w11, #0xFF, lsl #16
  cmp   w10, w11
  b.ls  20f                    // DTB fits — proceed
  //
  // DTB too large for the reserved slot — this is fatal.  Print an error
  // breadcrumb and deadloop rather than silently corrupting the firmware.
  //
  mov   w16, #'!'
  str   w16, [x15]
  mov   w16, #'F'
  str   w16, [x15]
  mov   w16, #'\n'
  str   w16, [x15]
  b     _NeverReturn
20:
  str   x27, [x8]

  mov   x0, x27
  mov   x1, x28
  mov   x19, x15            // save UART base
  bl    CopyFdt
  mov   x15, x19            // restore UART base

.Lout:
  ret    x29

.LArm64LinuxMagic:
  .byte   0x41, 0x52, 0x4d, 0x64

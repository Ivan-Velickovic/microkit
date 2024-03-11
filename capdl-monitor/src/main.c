/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
/*
 * The seL4 Core Platform Monitor.
 *
 * The monitor's purpose is to act as the fault handler for for protection domains.
 */

/*
 * Why this you may ask? Well, the seL4 headers depend on
 * a global `__sel4_ipc_buffer` which is a pointer to the
 * thread's IPC buffer. Which is reasonable enough, passing
 * that explicitly to every function would be annoying.
 *
 * The seL4 headers make this global a thread-local global,
 * which is also reasonable, considering it applies to a
 * specific thread! But, for our purposes we don't have threads!
 *
 * Thread local storage is painful and annoying to configure.
 * We'd really rather NOT use thread local storage (especially
 * consider we never have more than one thread in a Vspace)
 *
 * So, by defining __thread to be empty it means the variable
 * becomes a true global rather than thread local storage
 * variable, which means, we don't need to waste a bunch
 * of effort and complexity on thread local storage implementation.
 */
#define __thread

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sel4/sel4.h>

#include "util.h"

#define FAULT_EP_CAP_IDX 2
#define REPLY_CAP_IDX 3

#define MAX_PDS 64
#define MAX_NAME_LEN 16
#define MAX_TCBS 64

extern seL4_IPCBuffer __sel4_ipc_buffer_obj;
seL4_IPCBuffer *__sel4_ipc_buffer = &__sel4_ipc_buffer_obj;

char _stack[4096];

// static char pd_names[MAX_PDS][MAX_NAME_LEN];

// seL4_Word tcbs[MAX_TCBS];

extern const void (*const __init_array_start []) (void);
extern const void (*const __init_array_end []) (void);

static void
run_init_funcs(void)
{
    size_t count = __init_array_end - __init_array_start;
    for (size_t i = 0; i < count; i++) {
        __init_array_start[i]();
    }
}

static char *
ec_to_string(uintptr_t ec)
{
    switch (ec) {
        case 0: return "Unknown reason";
        case 1: return "Trapped WFI or WFE instruction execution";
        case 3: return "Trapped MCR or MRC access with (coproc==0b1111) this is not reported using EC 0b000000";
        case 4: return "Trapped MCRR or MRRC access with (coproc==0b1111) this is not reported using EC 0b000000";
        case 5: return "Trapped MCR or MRC access with (coproc==0b1110)";
        case 6: return "Trapped LDC or STC access";
        case 7: return "Access to SVC, Advanced SIMD or floating-point functionality trapped";
        case 12: return "Trapped MRRC access with (coproc==0b1110)";
        case 13: return "Branch Target Exception";
        case 17: return "SVC instruction execution in AArch32 state";
        case 21: return "SVC instruction execution in AArch64 state";
        case 24: return "Trapped MSR, MRS or System instruction exuection in AArch64 state, this is not reported using EC 0xb000000, 0b000001 or 0b000111";
        case 25: return "Access to SVE functionality trapped";
        case 28: return "Exception from a Pointer Authentication instruction authentication failure";
        case 32: return "Instruction Abort from a lower Exception level";
        case 33: return "Instruction Abort taken without a change in Exception level";
        case 34: return "PC alignment fault exception";
        case 36: return "Data Abort from a lower Exception level";
        case 37: return "Data Abort taken without a change in Exception level";
        case 38: return "SP alignment faultr exception";
        case 40: return "Trapped floating-point exception taken from AArch32 state";
        case 44: return "Trapped floating-point exception taken from AArch64 state";
        case 47: return "SError interrupt";
        case 48: return "Breakpoint exception from a lower Exception level";
        case 49: return "Breakpoint exception taken without a change in Exception level";
        case 50: return "Software Step exception from a lower Exception level";
        case 51: return "Software Step exception taken without a change in Exception level";
        case 52: return "Watchpoint exception from a lower Exception level";
        case 53: return "Watchpoint exception taken without a change in Exception level";
        case 56: return "BKPT instruction execution in AArch32 state";
        case 60: return "BRK instruction execution in AArch64 state";
    }
    return "<invalid EC>";
}

static char *
data_abort_dfsc_to_string(uintptr_t dfsc)
{
    switch(dfsc) {
        case 0x00: return "address size fault, level 0";
        case 0x01: return "address size fault, level 1";
        case 0x02: return "address size fault, level 2";
        case 0x03: return "address size fault, level 3";
        case 0x04: return "translation fault, level 0";
        case 0x05: return "translation fault, level 1";
        case 0x06: return "translation fault, level 2";
        case 0x07: return "translation fault, level 3";
        case 0x09: return "access flag fault, level 1";
        case 0x0a: return "access flag fault, level 2";
        case 0x0b: return "access flag fault, level 3";
        case 0x0d: return "permission fault, level 1";
        case 0x0e: return "permission fault, level 2";
        case 0x0f: return "permission fault, level 3";
        case 0x10: return "synchronuos external abort";
        case 0x11: return "synchronous tag check fault";
        case 0x14: return "synchronous external abort, level 0";
        case 0x15: return "synchronous external abort, level 1";
        case 0x16: return "synchronous external abort, level 2";
        case 0x17: return "synchronous external abort, level 3";
        case 0x18: return "syncrhonous partity or ECC error";
        case 0x1c: return "syncrhonous partity or ECC error, level 0";
        case 0x1d: return "syncrhonous partity or ECC error, level 1";
        case 0x1e: return "syncrhonous partity or ECC error, level 2";
        case 0x1f: return "syncrhonous partity or ECC error, level 3";
        case 0x21: return "alignment fault";
        case 0x30: return "tlb conflict abort";
        case 0x31: return "unsupported atomic hardware update fault";
    }
    return "<unexpected DFSC>";
}

static void
monitor(void)
{
    for (;;) {
        seL4_Word badge, label;
        seL4_MessageInfo_t tag;
        // seL4_Error err;

        tag = seL4_Recv(FAULT_EP_CAP_IDX, &badge, REPLY_CAP_IDX);
        label = seL4_MessageInfo_get_label(tag);

        // seL4_Word tcb_cap = tcbs[badge];

        puts("MON|INFO: received message ");
        puthex32(label);
        puts("  badge: ");
        puthex64(badge);
        // puts("  tcb cap: ");
        // puthex64(tcb_cap);
        puts("\n");

        // if (badge < MAX_PDS && pd_names[badge][0] != 0) {
        //     puts("faulting PD: ");
        //     puts(pd_names[badge]);
        //     puts("\n");
        // } else {
        //     puts("unknown/invalid badge\n");
        // }

        // seL4_UserContext regs;

        // err = seL4_TCB_ReadRegisters(tcb_cap, false, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &regs);
        // if (err != seL4_NoError) {
        //     fail("error reading registers");
        // }

        // // FIXME: Would be good to print the whole register set
        // puts("Registers: \n");
        // puts("pc : ");
        // puthex64(regs.pc);
        // puts("\n");
        // puts("spsr : ");
        // puthex64(regs.spsr);
        // puts("\n");
        // puts("x0 : ");
        // puthex64(regs.x0);
        // puts("\n");
        // puts("x1 : ");
        // puthex64(regs.x1);
        // puts("\n");
        // puts("x2 : ");
        // puthex64(regs.x2);
        // puts("\n");
        // puts("x3 : ");
        // puthex64(regs.x3);
        // puts("\n");
        // puts("x4 : ");
        // puthex64(regs.x4);
        // puts("\n");
        // puts("x5 : ");
        // puthex64(regs.x5);
        // puts("\n");
        // puts("x6 : ");
        // puthex64(regs.x6);
        // puts("\n");
        // puts("x7 : ");
        // puthex64(regs.x7);
        // puts("\n");

        switch(label) {
            case seL4_Fault_CapFault: {
                seL4_Word ip = seL4_GetMR(seL4_CapFault_IP);
                seL4_Word fault_addr = seL4_GetMR(seL4_CapFault_Addr);
                seL4_Word in_recv_phase = seL4_GetMR(seL4_CapFault_InRecvPhase);
                seL4_Word lookup_failure_type = seL4_GetMR(seL4_CapFault_LookupFailureType);
                seL4_Word bits_left = seL4_GetMR(seL4_CapFault_BitsLeft);
                seL4_Word depth_bits_found = seL4_GetMR(seL4_CapFault_DepthMismatch_BitsFound);
                seL4_Word guard_found = seL4_GetMR(seL4_CapFault_GuardMismatch_GuardFound);
                seL4_Word guard_bits_found = seL4_GetMR(seL4_CapFault_GuardMismatch_BitsFound);

                puts("CapFault: ip=");
                puthex64(ip);
                puts("  fault_addr=");
                puthex64(fault_addr);
                puts("  in_recv_phase=");
                puts(in_recv_phase == 0 ? "false" : "true");
                puts("  lookup_failure_type=");

                switch (lookup_failure_type) {
                    case seL4_NoFailure: puts("seL4_NoFailure"); break;
                    case seL4_InvalidRoot: puts("seL4_InvalidRoot"); break;
                    case seL4_MissingCapability: puts("seL4_MissingCapability"); break;
                    case seL4_DepthMismatch: puts("seL4_DepthMismatch"); break;
                    case seL4_GuardMismatch: puts("seL4_GuardMismatch"); break;
                    default: puthex64(lookup_failure_type);
                }

                if (
                    lookup_failure_type == seL4_MissingCapability ||
                    lookup_failure_type == seL4_DepthMismatch ||
                    lookup_failure_type == seL4_GuardMismatch) {
                    puts("  bits_left=");
                    puthex64(bits_left);
                }
                if (lookup_failure_type == seL4_DepthMismatch) {
                    puts("  depth_bits_found=");
                    puthex64(depth_bits_found);
                }
                if (lookup_failure_type == seL4_GuardMismatch) {
                    puts("  guard_found=");
                    puthex64(guard_found);
                    puts("  guard_bits_found=");
                    puthex64(guard_bits_found);
                }
                puts("\n");
                break;
            }
            case seL4_Fault_UserException: {
                seL4_Word ip = seL4_GetMR(seL4_UserException_FaultIP);
                seL4_Word sp = seL4_GetMR(seL4_UserException_SP);
                seL4_Word flags = seL4_GetMR(seL4_UserException_FLAGS);
                seL4_Word number = seL4_GetMR(seL4_UserException_Number);
                seL4_Word code = seL4_GetMR(seL4_UserException_Code);
                puts("UserException: ip=");
                puthex64(ip);
                puts("  sp=");
                puthex64(sp);
                puts("  flags=");
                puthex64(flags);
                puts("  number=");
                puthex64(number);
                puts("  code=");
                puthex64(code);
                puts("\n");
                break;
            }
            case seL4_Fault_VMFault: {
                seL4_Word ip = seL4_GetMR(seL4_VMFault_IP);
                seL4_Word fault_addr = seL4_GetMR(seL4_VMFault_Addr);
                seL4_Word is_instruction = seL4_GetMR(seL4_VMFault_PrefetchFault);
                seL4_Word fsr = seL4_GetMR(seL4_VMFault_FSR);
                seL4_Word ec = fsr >> 26;
                seL4_Word il = fsr >> 25 & 1;
                seL4_Word iss = fsr & 0x1ffffffUL;
                puts("VMFault: ip=");
                puthex64(ip);
                puts("  fault_addr=");
                puthex64(fault_addr);
                puts("  fsr=");
                puthex64(fsr);
                puts("  ");
                puts(is_instruction ? "(instruction fault)" : "(data fault)");
                puts("\n");
                puts("   ec: ");
                puthex32(ec);
                puts("  ");
                puts(ec_to_string(ec));
                puts("   il: ");
                puts(il ? "1" : "0");
                puts("   iss: ");
                puthex32(iss);
                puts("\n");

                if (ec == 0x24) {
                    /* FIXME: Note, this is not a complete decoding of the fault! Just some of the more
                       common fields!
                    */
                    seL4_Word dfsc = iss & 0x3f;
                    bool ea = (iss >> 9) & 1;
                    bool cm = (iss >> 8) & 1;
                    bool s1ptw = (iss >> 7) & 1;
                    bool wnr = (iss >> 6) & 1;
                    puts("   dfsc = ");
                    puts(data_abort_dfsc_to_string(dfsc));
                    puts(" (");
                    puthex32(dfsc);
                    puts(")");
                    if (ea) {
                        puts(" -- external abort");
                    }
                    if (cm) {
                        puts(" -- cache maint");
                    }
                    if (s1ptw) {
                        puts(" -- stage 2 fault for stage 1 page table walk");
                    }
                    if (wnr) {
                        puts(" -- write not read");
                    }
                    puts("\n");
                }

                break;
            }
            default:
                puts("Unknown fault\n");
                break;
        }
    }
}

void
main()
{
    run_init_funcs();
    puts("MON|INFO: started, waiting for faults to come in\n");
    monitor();
}
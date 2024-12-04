/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>

char *elf_a;
char *elf_b;

char *pd_code;

static char hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

static void puthex64(uint64_t val)
{
    char buffer[16 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[16 + 3 - 1] = 0;
    for (unsigned i = 16 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    microkit_dbg_puts(buffer);
}

static char decchar(unsigned int v)
{
    return '0' + v;
}

static void put8(uint8_t x)
{
    char tmp[4];
    unsigned i = 3;
    tmp[3] = 0;
    do {
        uint8_t c = x % 10;
        tmp[--i] = decchar(c);
        x /= 10;
    } while (x);
    microkit_dbg_puts(&tmp[i]);
}


void init(void)
{
    microkit_dbg_puts("ELF TESTER starting\n");
    if (elf_b[0] == 0x94) {
        microkit_dbg_puts("yay!\n");
    }
}

int pd = 0;

void notified(microkit_channel ch)
{
    microkit_pd_stop(0);

    char *new_pd;
    if (pd % 2 == 0) {
        new_pd = elf_b;
        microkit_dbg_puts("switching to B\n");
    } else {
        new_pd = elf_a;
        microkit_dbg_puts("switching to A\n");
    }
    pd++;

    for (int i = 0; i < 0x2000; i++) {
        pd_code[i] = new_pd[i];
    }

    seL4_Error err;
    seL4_UserContext ctxt = {0};
    ctxt.pc = 0x200000;
    ctxt.sp = 0x0000010000000000;
    err = seL4_TCB_WriteRegisters(
              BASE_TCB_CAP + 0,
              seL4_True,
              0, /* No flags */
              2, /* writing 1 register */
              &ctxt
          );
    if (err != seL4_NoError) {
        microkit_dbg_puts("uh oh\n");
    }
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    switch (label) {
        case seL4_Fault_VMFault: {
            microkit_dbg_puts("got vm fault\n");
            seL4_Word ip = seL4_GetMR(seL4_VMFault_IP);
            seL4_Word fault_addr = seL4_GetMR(seL4_VMFault_Addr);
            puthex64(ip);
            microkit_dbg_puts("\n");
            puthex64(fault_addr);
            microkit_dbg_puts("\n");
            break;
        }
        default:
            microkit_dbg_puts("unknown\n");
            break;
    }
    return seL4_False;
}

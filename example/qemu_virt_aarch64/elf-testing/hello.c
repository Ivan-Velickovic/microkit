/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>

uint64_t hello = 5;

uintptr_t testing;

void init(void)
{
    microkit_dbg_puts("hello, world\n");
    if (hello == 5) {
        microkit_dbg_puts("yay from a\n");
    }

    if (testing != 0x10000000) {
        microkit_dbg_puts("UH OH\n");
    }

    microkit_notify(1);
}

void notified(microkit_channel ch)
{
}

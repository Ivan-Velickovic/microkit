/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>

uint64_t hello = 10;

uintptr_t testing;

void init(void)
{
    microkit_dbg_puts("hello, world, from b!\n");
    if (hello == 10) {
        microkit_dbg_puts("yay from b\n");
    }

    if (testing != 0x10000000) {
        microkit_dbg_puts("UH OH\n");
    }

    microkit_notify(1);
}

void notified(microkit_channel ch)
{
}

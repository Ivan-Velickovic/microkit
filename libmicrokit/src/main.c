/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define __thread
#include <sel4/sel4.h>

#include <microkit.h>

#define INPUT_CAP 1

#define PD_MASK 0xff
#define CHANNEL_MASK 0x3f

char _stack[4096]  __attribute__((__aligned__(16)));

bool passive = false;
char microkit_name[16];
bool have_signal = false;
seL4_CPtr signal_cap;
seL4_MessageInfo_t signal_msg;

extern seL4_IPCBuffer __sel4_ipc_buffer_obj;

seL4_IPCBuffer *__sel4_ipc_buffer = &__sel4_ipc_buffer_obj;

extern const void (*const __init_array_start []) (void);
extern const void (*const __init_array_end []) (void);

__attribute__((weak)) microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return seL4_MessageInfo_new(0, 0, 0, 0);
}

__attribute__((weak)) void fault(microkit_id id, microkit_msginfo msginfo)
{
}

static void
run_init_funcs(void)
{
    size_t count = __init_array_end - __init_array_start;
    for (size_t i = 0; i < count; i++) {
        __init_array_start[i]();
    }
}

static void
handler_loop(void)
{
    bool have_reply = false;
    seL4_MessageInfo_t reply_tag;

    for (;;) {
        seL4_Word badge;
        seL4_MessageInfo_t tag;

        if (have_reply) {
            tag = seL4_ReplyRecv(INPUT_CAP, reply_tag, &badge, REPLY_CAP);
        } else if (have_signal) {
            tag = seL4_NBSendRecv(signal_cap, signal_msg, INPUT_CAP, &badge, REPLY_CAP);
        } else {
            tag = seL4_Recv(INPUT_CAP, &badge, REPLY_CAP);
        }

        uint64_t is_endpoint = badge >> 63;
        uint64_t is_fault = (badge >> 62) & 1;

        have_reply = false;
        have_signal = false;

        if (is_fault) {
            fault(badge & PD_MASK, tag);
        } else if (is_endpoint) {
            have_reply = true;
            reply_tag = protected(badge & CHANNEL_MASK, tag);
        } else {
            unsigned int idx = 0;
            do  {
                if (badge & 1) {
                    notified(idx);
                }
                badge >>= 1;
                idx++;
            } while (badge != 0);
        }
    }
}

void
main(void)
{
    run_init_funcs();
    init();

    /*
     * If we are passive, now our initialisation is complete we can
     * signal the monitor to unbind our scheduling context and bind
     * it to our notification object.
     * We delay this signal so we are ready waiting on a recv() syscall
     */
    if (passive) {
        have_signal = true;
        signal_msg = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, 0);
        signal_cap = (MONITOR_ENDPOINT_CAP);
    }

    handler_loop();
}

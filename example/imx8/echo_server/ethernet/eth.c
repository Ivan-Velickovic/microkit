/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <sel4cp.h>
#include "eth.h"

#define OUTPUT_CH 1
#define INPUT_CH 2
#define IRQ_CH 3

#define CCM_VADDR   0x2200000
#define MDC_FREQ    20000000UL

uintptr_t hw_ring_buffer_vaddr;
uintptr_t hw_ring_buffer_paddr;

uintptr_t packet_buffer_vaddr;
uintptr_t packet_buffer_paddr;

/* Make the minimum frame buffer 2k. This is a bit of a waste of memory, but ensure alignment */
#define PACKET_BUFFER_SIZE (2 * 1024)
#define MAX_PACKET_SIZE     1536

#define RBD_COUNT 128
#define TBD_COUNT 128

struct rbd {
    uint16_t data_length;
    uint16_t flags;
    uint32_t addr;
};

struct tbd {
    uint16_t data_length;
    uint16_t flags;
    uint32_t addr;
};

static uint8_t mac[6];

static unsigned rbd_index = 0;
//static unsigned tbd_index = 0;

volatile struct enet_regs *eth = (void *)(uintptr_t)0x2000000;

// clock controller. TODO: This should be abstracted out. 
volatile uint32_t *ccgr_enet_set = (void *)(uintptr_t)CCM_VADDR + 0x40a0;
volatile uint32_t *ccgr_enet_clr = (void *)(uintptr_t)CCM_VADDR + 0x40a4;
volatile uint32_t *ccgr_sim_enet_set = (void *)(uintptr_t)CCM_VADDR + 0x4400;
volatile uint32_t *ccgr_sim_enet_clr = (void *)(uintptr_t)CCM_VADDR + 0x4404;
volatile uint32_t *enet_axi_target = (void *)(uintptr_t)CCM_VADDR + 0x8880;
volatile uint32_t *enet_ref_target = (void *)(uintptr_t)CCM_VADDR + 0xa980;
volatile uint32_t *enet_timer_target = (void *)(uintptr_t)CCM_VADDR + 0xaa00;

volatile struct rbd *rbd;
volatile struct tbd *tbd;

static char
hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

static void
dump_reg(const char *name, uint32_t val)
{

    char buffer[8 + 3 + 1];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[8 + 3 - 1] = 0;
    for (unsigned i = 8 + 1 + 1; i > 1; i--) {
        if (i == 6) {
            buffer[i] = '_';
        } else {
            buffer[i] = hexchar(val & 0xf);
            val >>= 4;
        }
    }
    sel4cp_dbg_puts(name);
    // unsigned int l = 10 - slen(name);
    // for (unsigned i = 0; i < l; i++) {
    //     sel4cp_dbg_putc(' ');
    // }
    sel4cp_dbg_puts(": ");
    sel4cp_dbg_puts(buffer);
    sel4cp_dbg_puts("\n");
}

static void
puthex64(uint64_t x)
{
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[2] = hexchar((x >> 60) & 0xf);
    buffer[3] = hexchar((x >> 56) & 0xf);
    buffer[4] = hexchar((x >> 52) & 0xf);
    buffer[5] = hexchar((x >> 48) & 0xf);
    buffer[6] = hexchar((x >> 44) & 0xf);
    buffer[7] = hexchar((x >> 40) & 0xf);
    buffer[8] = hexchar((x >> 36) & 0xf);
    buffer[9] = hexchar((x >> 32) & 0xf);
    buffer[10] = hexchar((x >> 28) & 0xf);
    buffer[11] = hexchar((x >> 24) & 0xf);
    buffer[12] = hexchar((x >> 20) & 0xf);
    buffer[13] = hexchar((x >> 16) & 0xf);
    buffer[14] = hexchar((x >> 12) & 0xf);
    buffer[15] = hexchar((x >> 8) & 0xf);
    buffer[16] = hexchar((x >> 4) & 0xf);
    buffer[17] = hexchar(x & 0xf);
    buffer[18] = 0;
    sel4cp_dbg_puts(buffer);
}

static void get_mac_addr(volatile struct enet_regs *reg, uint8_t *mac)
{
    uint32_t l, h;
    l = reg->palr;
    h = reg->paur;

    mac[0] = l >> 24;
    mac[1] = l >> 16 & 0xff;
    mac[2] = l >> 8 & 0xff;
    mac[3] = l & 0xff;
    mac[4] = h >> 24;
    mac[5] = h >> 16 & 0xff;
}

static void set_mac(volatile struct enet_regs *reg, uint8_t *mac)
{
    reg->palr = (mac[0] << 24) | (mac[1] << 16) | (mac[2] << 8) | (mac[3]);
    reg->paur = (mac[4] << 24) | (mac[5] << 16);
}

static void
dump_mac(uint8_t *mac)
{
    for (unsigned i = 0; i < 6; i++) {
        sel4cp_dbg_putc(hexchar((mac[i] >> 4) & 0xf));
        sel4cp_dbg_putc(hexchar(mac[i] & 0xf));
        if (i < 5) {
            sel4cp_dbg_putc(':');
        }
    }
}

static void
handle_rx(sel4cp_channel ch, volatile struct enet_regs *eth)
{
    uint16_t flags;

    for (;;) {
        flags = rbd[rbd_index].flags;
        //uint32_t packet_length = rbd[rbd_index].data_length;

        if ((flags & RXD_EMPTY)) {
            /* buffer is empty, can stop */
            break;
        }
            
        /* make it available */
        flags = RXD_EMPTY;
        if (rbd_index == RBD_COUNT - 1) {
            flags |= WRAP;
        }
        rbd[rbd_index].flags = flags;

        rbd_index++;
        if (rbd_index == RBD_COUNT) {
            rbd_index = 0;
        }
    }

    eth->rdar = RDAR_RDAR;
}

static void handle_eth(sel4cp_channel ch, volatile struct enet_regs *eth)
{
    uint32_t e = eth->eir & IRQ_MASK;
    /* write to clear events */
    eth->eir = e;

    while (e & IRQ_MASK) {
        if (e & NETIRQ_TXF) {
            sel4cp_dbg_puts("Transmit is complete");
            // complete_tx(dev);
        }
        if (e & NETIRQ_RXF) {
            sel4cp_dbg_puts("We got a packet!");
            handle_rx(ch, eth);
            //fill_rx_bufs(dev);
        }
        if (e & NETIRQ_EBERR) {
            sel4cp_dbg_puts("Error: System bus/uDMA");
            while (1);
        }
        e = eth->eir & IRQ_MASK;
        /* write to clear events */
        eth->eir = e;
    }
}

static void eth_setup(void)
{
    get_mac_addr(eth, mac);
    sel4cp_dbg_puts("MAC: ");
    dump_mac(mac);
    sel4cp_dbg_puts("\n");

    rbd = (void *)hw_ring_buffer_vaddr;
    tbd = (void *)(hw_ring_buffer_vaddr + (sizeof(struct rbd) * RBD_COUNT));

    for (unsigned i = 0; i < RBD_COUNT; i++) {
        rbd[i].data_length = 0;
        rbd[i].flags = RXD_EMPTY;
        rbd[i].addr = packet_buffer_paddr + (i * PACKET_BUFFER_SIZE);
    }

    for (unsigned i = 0; i < TBD_COUNT; i++) {
        tbd[i].data_length = 0;
        tbd[i].flags = 0;
        tbd[i].addr = packet_buffer_paddr + ((RBD_COUNT + i) * PACKET_BUFFER_SIZE);
    }

    rbd[RBD_COUNT-1].flags |= WRAP;
    tbd[TBD_COUNT-1].flags |= WRAP;

    /* Perform reset */
    eth->ecr = ECR_RESET;
    while (eth->ecr & ECR_RESET);
    eth->ecr |= ECR_DBSWP;

    /* Clear and mask interrupts */
    eth->eimr = 0x00000000;
    eth->eir  = 0xffffffff;

    /* Gate the clocks first */
    *ccgr_enet_clr = 0x3;
    *ccgr_sim_enet_clr = 0x3;
    /* Set up the clocks */
    *enet_axi_target = (1UL << 28) | 0x01000000; // ENABLE | MUX SYS1_PLL | POST AND PRE DIVIDE BY 1
    *enet_ref_target = (1UL << 28) | 0x01000000; // ENABLE | MUX PLL2_DIV8 | POST AND PRE DIVIDE BY 1
    *enet_timer_target = (1UL << 28) | 0x01000000 | ((4) & 0x3f); // ENABLE | MUX PLL2_DIV10 | POST DIVIDE BY 4, PRE DIVIDE BY 1
    /* Ungate the clocks now */
    *ccgr_enet_set = 0x3;
    *ccgr_sim_enet_set = 0x3;

    /* set MDIO freq */
    eth->mscr = 24 << 1;

    /* Disable */
    eth->mibc |= MIBC_DIS;
    while (!(eth->mibc & MIBC_IDLE));
    /* Clear */
    eth->mibc |= MIBC_CLEAR;
    while (!(eth->mibc & MIBC_IDLE));
    /* Restart */
    eth->mibc &= ~MIBC_CLEAR;
    eth->mibc &= ~MIBC_DIS;

    /* Descriptor group and individual hash tables - Not changed on reset */
    eth->iaur = 0;
    eth->ialr = 0;
    eth->gaur = 0;
    eth->galr = 0;

    if (eth->palr == 0) {
        // the mac address needs setting again. 
        set_mac(eth, mac);
    }

    eth->opd = PAUSE_OPCODE_FIELD;
    eth->tipg = TIPG;
    /* Transmit FIFO Watermark register - store and forward */
    eth->tfwr = 0;
    /* Do not forward frames with errors */
    eth->racc = RACC_LINEDIS;

    /* Set RDSR */
    sel4cp_dbg_puts("RING BUFFER ADDR=: ");
    puthex64((uintptr_t)hw_ring_buffer_paddr);
    sel4cp_dbg_puts("\n");

    eth->rdsr = hw_ring_buffer_paddr;
    eth->tdsr = hw_ring_buffer_paddr + (sizeof(struct rbd) * RBD_COUNT);

    /* Size of max eth packet size */
    eth->mrbr = MAX_PACKET_SIZE;

    eth->rcr = RCR_MAX_FL(1518) | RCR_RGMII_EN | RCR_MII_MODE;
    eth->tcr = TCR_FDEN;

    /* set speed */
    eth->ecr |= ECR_SPEED;

    /* Set Enable  in ECR */
    eth->ecr |= ECR_ETHEREN;
    //dump_reg("rcr", eth->rcr);
    //dump_reg("ecr", eth->ecr);

    eth->rdar = RDAR_RDAR;

    /* enable events */
    eth->eir = eth->eir;
    eth->eimr = IRQ_MASK;

    sel4cp_dbg_puts(sel4cp_name);
    sel4cp_dbg_puts(": init complete -- waiting for interrupt\n");
}

void init(void)
{
    sel4cp_dbg_puts(sel4cp_name);
    sel4cp_dbg_puts(": elf PD init function running\n");

    eth_setup();
    sel4cp_dbg_puts("eth->eimr = ");
    puthex64(eth->eimr);
    sel4cp_dbg_puts("\n");
    sel4cp_dbg_puts("eth->eir = ");
    puthex64(eth->eir);
    sel4cp_dbg_puts("\n");
}

void notified(sel4cp_channel ch)
{
    sel4cp_dbg_puts("We got a notification");
    dump_reg("CH", ch);
    switch(ch) {
        case IRQ_CH:
            handle_eth(ch, eth);
            sel4cp_irq_ack(ch);
            break;
        /* this is where our transmit notification would come in */
        default:
            sel4cp_dbg_puts("eth driver: received notification on unexpected channel\n");
            //dump_reg("CH", ch);
            break;
    }
}
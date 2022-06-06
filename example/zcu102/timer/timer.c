#include <stdint.h>
#include <stdbool.h>
#include <sel4cp.h>

/*
TODO/notes:
* compiler fence may be necessary, not sure, runs fine without.

* right now we're clearing the interrupt status regardless of the interrupt,
  we may also need to clear it in other cases/not be clearing it all the time.
  Have a look at the "Programming notes" section of the TRM and the linux
  driver: https://github.com/Xilinx/linux-xlnx/blob/master/drivers/rtc/rtc-zynqmp.c

* Would be nice to have proper printf lmao.
*/

/* Util stuff */
#define BIT(x)  (1UL << (x))

static void assert(bool value)
{
    if (value) {
        return;
    }

    sel4cp_dbg_puts("Assertion failed\n");
    asm volatile("udf 0");
}

static char hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

static void puthex32(uint32_t x)
{
    char buffer[11];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[2] = hexchar((x >> 28) & 0xf);
    buffer[3] = hexchar((x >> 24) & 0xf);
    buffer[4] = hexchar((x >> 20) & 0xf);
    buffer[5] = hexchar((x >> 16) & 0xf);
    buffer[6] = hexchar((x >> 12) & 0xf);
    buffer[7] = hexchar((x >> 8) & 0xf);
    buffer[8] = hexchar((x >> 4) & 0xf);
    buffer[9] = hexchar(x & 0xf);
    buffer[10] = 0;
    sel4cp_dbg_puts(buffer);
}

uintptr_t rtclock_vaddr __attribute__ ((section (".data")));

/*
 * RTC clock register map (page 176).
 * Zynq UltraScale+ Device TRM UG1085 (v2.2) December 4, 2020.
 *
 * Also see register reference: https://www.xilinx.com/htmldocs/registers/ug1087/ug1087-zynq-ultrascale-registers.html
 */
#define RTC_SET_TIME_WRITE          0x000   /* width 32, WO */
#define RTC_SET_TIME_READ           0x004   /* width 32, RO */
#define RTC_CALIB_WRITE             0x008   /* width 21, WO */
#define RTC_CALIB_READ              0x00C   /* width 21, RO */
#define RTC_CURRENT_TIME            0x010   /* width 32, RO */
#define RTC_ALARM                   0x018   /* width 32, RW */
#define RTC_INT_STATUS              0x020   /* width 2, Write to clear */
#define RTC_INT_MASK                0x024   /* width 2, RO */
#define RTC_INT_ENABLE              0x028   /* width 2, WO */
#define RTC_INT_DISABLE             0x02C   /* width 2, WO */
#define RTC_ADDR_ERROR              0x030   /* width 1, Write to clear */
#define RTC_ADDR_ERROR_INT_MASK     0x034   /* width 1, RO */
#define RTC_ADDR_ERROR_INT_ENABLE   0x038   /* width 1, WO */
#define RTC_ADDR_ERROR_INT_DISABLE  0x03C   /* width 1, WO */
#define RTC_CONTROL                 0x040   /* width 32, RW */
#define RTC_SAFETY_CHK              0x050   /* width 32, RW */

#define SEL4CP_RTC_ALARM_INTID     8
#define SEL4CP_RTC_SECONDS_INTID   9

#define RTC_CALIB_MASK      0x1FFFFF
#define RTC_REG(offset) ((volatile uint32_t*)(rtclock_vaddr + offset))
#define RTC_ALARM_MASK      BIT(1)

void clear_int_status()
{
    *RTC_REG(RTC_INT_STATUS) = 3;
}

uint32_t read_time()
{
    uint32_t status = *RTC_REG(RTC_INT_STATUS);
    if (status & 1) {
        return *RTC_REG(RTC_CURRENT_TIME);
    } else {
        return *RTC_REG(RTC_SET_TIME_READ) - 1;
    }
}

void set_time(uint32_t to)
{
    // p177 programming notes
    *RTC_REG(RTC_CALIB_WRITE) = 0x198231;
    *RTC_REG(RTC_SET_TIME_WRITE) = to + 1;
    *RTC_REG(RTC_INT_STATUS) = 1;
}

void set_alarm(uint32_t seconds)
{
    uint32_t target = read_time() + seconds;
    *RTC_REG(RTC_ALARM) = target;
    clear_int_status();
}

void init_time()
{
    *RTC_REG(RTC_CALIB_WRITE) = 0x198231;
    *RTC_REG(RTC_CONTROL) = BIT(31) | BIT(24);
    clear_int_status();
    *RTC_REG(RTC_INT_ENABLE) = 0x03;
}

void init(void)
{
    init_time();
    sel4cp_dbg_puts("timer: started\n");

    sel4cp_dbg_puts("timer: reading time\n");
    uint32_t current_time = read_time();
    sel4cp_dbg_puts("timer: read ");
    puthex32(current_time);
    sel4cp_dbg_puts("\n");

    sel4cp_dbg_puts("timer: setting time\n");
    set_time(0xdeadbeef);
    assert(read_time() == 0xdeadbeef);
}

void notified(sel4cp_channel ch)
{
    /* I think we need to clear after reading time. */
    uint32_t current_time = read_time();
    clear_int_status();

    switch (ch) {
    case SEL4CP_RTC_ALARM_INTID:
        break;
    case SEL4CP_RTC_SECONDS_INTID:
        sel4cp_dbg_puts("timer: read time ");
        puthex32(current_time);
        sel4cp_dbg_puts("\n");
        break;
    default:
        sel4cp_dbg_puts("timer: unknown notification fault\n");
        assert(false);
        break;
    }

    sel4cp_irq_ack(ch);
    /* And I think we need to clear after "handling" an interrupt.
       Basically acking it, but sel4cp_irq_ack only deals with acking
       it with seL4 (?). */
    clear_int_status();
}

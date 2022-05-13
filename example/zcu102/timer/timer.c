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

static void assert(bool value) {
    if (value) return;

    sel4cp_dbg_puts("Assertion failed\n");
    while (true) {}
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

/*****************************************************************************/

uintptr_t rtclock_vaddr;

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
#define RTC_REG(offset) (rtclock_vaddr + offset);

#define ALARM_NUM_SECONDS 4


uint32_t read_time()
{
    return *(volatile uint32_t*) RTC_REG(RTC_CURRENT_TIME);
}

void set_time(uint32_t to)
{
    *(volatile uint32_t*) (rtclock_vaddr + RTC_SET_TIME_WRITE) = to;
}

void set_alarm(uint32_t seconds)
{
    uint32_t target = read_time() + seconds;
    *(volatile uint32_t*) (rtclock_vaddr + RTC_ALARM) = target;
}

void init_time()
{
    // Xilinx Zynq UltraScale+ user guide p178:
    // Init RTC programming sequence

    volatile uint32_t* rtc_int_disable = (volatile uint32_t*) RTC_REG(RTC_INT_DISABLE);
    *rtc_int_disable = 0x03; // @ivanv: check

    volatile uint32_t* rtc_int_enable = (volatile uint32_t*) RTC_REG(RTC_INT_ENABLE);
    *rtc_int_enable = 0x03; // @ivanv: check

    volatile uint32_t* rtc_calib_write = (volatile uint32_t*) RTC_REG(RTC_CALIB_WRITE);
    /* Want to reset then add mask which is why we're doing = and not &= */
    *rtc_calib_write = RTC_CALIB_MASK;

    volatile uint32_t* rtc_calib_read = (volatile uint32_t*) RTC_REG(RTC_CALIB_READ);

    assert(rtc_calib_read[0] == RTC_CALIB_MASK);

    volatile uint32_t* rtc_alarm = (volatile uint32_t*) RTC_REG(RTC_ALARM);
    rtc_alarm[0] = 0;

    volatile uint32_t* rtc_control = (volatile uint32_t*) RTC_REG(RTC_CONTROL);
    rtc_control[0] = BIT(24);
    // rtc_control[0] &= ~BIT(31);

    volatile uint32_t* rtc_int_status = (volatile uint32_t*) RTC_REG(RTC_INT_STATUS);
    *rtc_int_status = 0;
}

static inline void clear_int_status() {
    volatile uint32_t* rtc_int_status = (volatile uint32_t*) RTC_REG(RTC_INT_STATUS);
    *rtc_int_status = 0x03;
}

/*****************************************************************************/

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
    set_time(0xDEAFBABE);

    sel4cp_dbg_puts("timer: reading time\n");
    current_time = read_time();
    sel4cp_dbg_puts("timer: read ");
    puthex32(current_time);
    sel4cp_dbg_puts("\n");

    sel4cp_dbg_puts("timer: setting alarm for 4 seconds\n");
    // FIXME: why does the alarm go off at 0xdeafbac2 instead of 0xdeafbac3?
    set_alarm(ALARM_NUM_SECONDS);
}

void notified(sel4cp_channel ch)
{
    /* I think we need to clear after reading time. */
    uint32_t current_time = read_time();
    clear_int_status();

    switch(ch) {
        case SEL4CP_RTC_ALARM_INTID:
            sel4cp_dbg_puts("timer: alarm at time ");
            puthex32(current_time);
            sel4cp_dbg_puts("\n");
            set_alarm(ALARM_NUM_SECONDS);
            sel4cp_irq_ack(ch);
            break;
        case SEL4CP_RTC_SECONDS_INTID:
            sel4cp_dbg_puts("timer: read time ");
            puthex32(current_time);
            sel4cp_dbg_puts("\n");
            sel4cp_irq_ack(ch);
            break;
        default:
            sel4cp_dbg_puts("timer: unknown notification fault\n");
            break;
    }

    /* And I think we need to clear after "handling" an interrupt.
       Basically acking it, but sel4cp_irq_ack only deals with acking
       it with seL4 (?). */
    clear_int_status();
}

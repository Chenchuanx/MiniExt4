#include <drivers/pit.h>
#include <lib/port.h>

static volatile uint32_t g_pit_ticks = 0;
static volatile uint32_t g_pit_hz = 1000;

extern "C" void pit_init(uint32_t hz)
{
    if (hz == 0) {
        hz = 1000;
    }

    if (hz > 1193182u) {
        hz = 1193182u;
    }

    uint32_t divisor = 1193182u / hz;
    if (divisor == 0) {
        divisor = 1;
    }

    g_pit_hz = 1193182u / divisor;

    Port8Bit command(0x43);
    Port8Bit channel0(0x40);

    // Channel 0, lobyte/hibyte, mode 2 (rate generator), binary counter.
    command.Write(0x34);
    channel0.Write((uint8_t)(divisor & 0xFF));
    channel0.Write((uint8_t)((divisor >> 8) & 0xFF));
}

extern "C" void pit_on_irq_tick(void)
{
    g_pit_ticks++;
}

extern "C" uint32_t pit_get_ticks(void)
{
    return g_pit_ticks;
}

extern "C" uint32_t pit_get_frequency_hz(void)
{
    return g_pit_hz;
}

extern "C" uint32_t pit_get_millis_in_second(void)
{
    uint32_t hz = g_pit_hz;
    if (hz == 0) {
        return 0;
    }

    uint32_t ticks = g_pit_ticks;
    uint32_t rem = ticks % hz;
    return (uint32_t)((rem * 1000u) / hz);
}

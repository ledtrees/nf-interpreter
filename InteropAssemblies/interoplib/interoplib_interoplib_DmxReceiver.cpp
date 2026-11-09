//-----------------------------------------------------------------------------
//
// LEDTREES: adapter from the DmxReceiver stubs to the ledtrees_dmx component.
//
// The UART interrupt handler, frame parsing and the last-frame buffer live in
// a separate IDF component - the ledtrees-idf-components repository,
// components/ledtrees_dmx. All that is left here is unwrapping the managed
// arrays and validating their sizes.
//
//-----------------------------------------------------------------------------

#include "interoplib.h"
#include "interoplib_interoplib_DmxReceiver.h"

#include <ledtrees_dmx.h>

using namespace interoplib::interoplib;

signed int DmxReceiver::NativeStart(
    signed int param0,
    signed int param1,
    signed int param2,
    signed int param3,
    HRESULT &hr)
{
    (void)hr;

    lt_dmx_config_t config;
    config.uartNum = param0;
    config.rxGpio = param1;
    config.reGpio = param2;
    config.deGpio = param3;

    return lt_dmx_start(&config);
}

void DmxReceiver::NativeStop(HRESULT &hr)
{
    (void)hr;

    lt_dmx_stop();
}

signed int DmxReceiver::NativeRead(uint16_t param0, CLR_RT_TypedArray_UINT8 param1, HRESULT &hr)
{
    if (param1.GetBuffer() == NULL)
    {
        hr = CLR_E_INVALID_PARAMETER;
        return 0;
    }

    return lt_dmx_read(param0, param1.GetBuffer(), param1.GetSize());
}

void DmxReceiver::NativeSetInverted(bool param0, HRESULT &hr)
{
    (void)hr;

    lt_dmx_set_inverted(param0);
}

void DmxReceiver::NativeGetStats(CLR_RT_TypedArray_UINT32 param0, HRESULT &hr)
{
    // managed passes DmxReceiver.StatCount items at least: anything shorter
    // would be a write past the end
    if (param0.GetBuffer() == NULL || param0.GetSize() < 7)
    {
        hr = CLR_E_INVALID_PARAMETER;
        return;
    }

    lt_dmx_stats_t stats;
    lt_dmx_get_stats(&stats);

    // CLR UINT32 is unsigned int, while uint32_t on Xtensa is unsigned long
    unsigned int *out = param0.GetBuffer();
    out[0] = stats.running;
    out[1] = stats.frames;
    out[2] = stats.rejectedStart;
    out[3] = stats.corrupted;
    out[4] = stats.overflows;
    out[5] = stats.lastSlots;
    out[6] = stats.ageMs;

    // appended later: written only when managed passes room for it
    if (param0.GetSize() >= 8)
    {
        out[7] = stats.breaks;
    }
}

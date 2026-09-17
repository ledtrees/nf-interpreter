//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include <target_platform.h>
#include <esp32_idf.h>
#include <nanoHAL_v2.h>
#include <nanoHAL_Boot.h>

// LEDTREES: unconditional, the download-boot request below needs it too. Chips whose
// RTC block was replaced by LP_AON (C6, H2, P4) have no such header and keep the weak
// default from src/HAL/nanoHAL_Boot.c.
#if __has_include(<soc/rtc_cntl_reg.h>)
#include <soc/rtc_cntl_reg.h>
#endif

inline void CPU_Reset()
{
#if CONFIG_IDF_TARGET_ESP32C3 && CONFIG_NF_WP_TRANSPORT_USB_CDC
    SET_PERI_REG_MASK(RTC_CNTL_OPTIONS0_REG, RTC_CNTL_SW_SYS_RST);
    while (true)
    {
    }
#else
    esp_restart();
#endif
};

// LEDTREES: on ESP32 the "proprietary bootloader" is the ROM download mode, and this is
// the only way into it without hands. The classic esptool auto-reset drives EN and IO0
// over DTR/RTS of an external UART bridge; on a board that talks through the chip's own
// USB those lines are virtual and wired to nothing, so the board has to be held in BOOT
// by a person. FORCE_DOWNLOAD_BOOT lives in the RTC domain, survives a software reset and
// makes the ROM enter download mode regardless of IO0.
//
// Resetting is left to the caller: the debugger answers the command first and reboots
// afterwards (CLR_RT_ExecutionEngine::Reboot sets RebootPending), so the client learns
// whether the request was accepted. The other caller, CLRStartup under
// RevertToBooterOnFault, never fires here - app_main zeroes CLR_SETTINGS and the flag
// stays false, so a faulting CLR still reboots into the application, not into the ROM.
#if defined(RTC_CNTL_OPTION1_REG) && defined(RTC_CNTL_FORCE_DOWNLOAD_BOOT)
bool RequestToLaunchProprietaryBootloader()
{
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    return true;
}
#endif

// CPU sleep is not currently implemented in this target
inline void CPU_Sleep(SLEEP_LEVEL_type level, uint64_t wakeEvents)
{
    (void)level;
    (void)wakeEvents;
};

inline bool CPU_IsSoftRebootSupported()
{
    return true;
};

void CPU_SetPowerMode(PowerLevel_type powerLevel)
{
    switch (powerLevel)
    {
        case PowerLevel__Off:
            // gracefully shutdown everything
            nanoHAL_Uninitialize_C(true);

            esp_deep_sleep_start();

            break;

        default:
            // all the other power modes are unsupported here
            break;
    }
}

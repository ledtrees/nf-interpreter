//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include <target_platform.h>
#include <esp32_idf.h>
#include <nanoHAL_v2.h>
#include <nanoHAL_Boot.h>

#if __has_include(<soc/rtc_cntl_reg.h>)
#include <soc/rtc_cntl_reg.h>
#endif
#if __has_include(<soc/lp_aon_reg.h>)
#include <soc/lp_aon_reg.h>
#endif
#if __has_include(<soc/lp_system_reg.h>)
#include <soc/lp_system_reg.h>
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

// The original ESP32 has no such bit and keeps the weak handler. Read-modify-write: the newer
// registers carry software reset bits next to this one.
#if defined(RTC_CNTL_FORCE_DOWNLOAD_BOOT)
#define NF_FORCE_DOWNLOAD_BOOT() REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT)
#elif defined(LP_AON_FORCE_DOWNLOAD_BOOT)
#define NF_FORCE_DOWNLOAD_BOOT() REG_SET_BIT(LP_AON_SYS_CFG_REG, LP_AON_FORCE_DOWNLOAD_BOOT)
#elif defined(LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT)
#define NF_FORCE_DOWNLOAD_BOOT() REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT)
#endif

// The proprietary bootloader of an ESP32 is the ROM download mode. FORCE_DOWNLOAD_BOOT survives
// a software reset and takes the ROM there regardless of the strapping pin; a power cycle clears
// it. The reset itself is left to the caller.
#ifdef NF_FORCE_DOWNLOAD_BOOT
bool RequestToLaunchProprietaryBootloader()
{
    NF_FORCE_DOWNLOAD_BOOT();
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

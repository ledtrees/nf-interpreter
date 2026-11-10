//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// LEDTREES: hang watchdog.
//
// Boards were found frozen with both cores idle: the CLR stopped, lwIP stopped
// answering ping, and nothing reset them - the task watchdog is not initialised
// and the interrupt watchdog only sees interrupts. This turns such a hang into
// a panic: the coredump keeps the backtraces of every task and the board
// reboots on its own.
//
// Two checks, made by a dedicated task:
// - the CLR feeds Watchdog_Reset() from its execution loop and from the event
//   wait loop, i.e. every tick even when the managed code is idle;
// - the lwIP tcpip thread runs a posted callback.
// If either is silent longer than HANG_TIMEOUT_US, esp_system_abort() ends in
// the panic handler, which writes the coredump and reboots.
//
// The RTC watchdog backs the task up: it is fed by the same task, so a hang
// that keeps even this task from running still resets the chip.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <lwip/opt.h>
#include <lwip/tcpip.h>
#include <soc/rtc.h>
#include <hal/wdt_hal.h>

#include <nanoHAL_Watchdog.h>

// Long enough for the slowest single native call the CLR makes without
// feeding (erasing an OTA partition, formatting storage).
#define HANG_TIMEOUT_US     (120LL * 1000 * 1000)
#define CHECK_PERIOD_MS     5000
#define RTC_WDT_TIMEOUT_MS  (180 * 1000)

static volatile int64_t s_clrFedUs;
static volatile int64_t s_tcpipRanUs;
static volatile bool s_tcpipSeen;

static wdt_hal_context_t s_rtcWdt = RWDT_HAL_CONTEXT_DEFAULT();

// targetHAL_HangWatchdog_Clr.cpp: the stuck managed thread, for the panic reason
void NF_HangWatchdog_DescribeClr(char *buf, size_t size);

void Watchdog_Reset()
{
    s_clrFedUs = esp_timer_get_time();
}

static void TcpipProbe(void *ctx)
{
    (void)ctx;

    s_tcpipRanUs = esp_timer_get_time();
    s_tcpipSeen = true;
}

static void RtcWdtStart(void)
{
    wdt_hal_init(&s_rtcWdt, WDT_RWDT, 0, false);
    uint32_t ticks = (uint32_t)((uint64_t)RTC_WDT_TIMEOUT_MS * rtc_clk_slow_freq_get_hz() / 1000ULL);

    wdt_hal_write_protect_disable(&s_rtcWdt);
    wdt_hal_config_stage(&s_rtcWdt, WDT_STAGE0, ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_enable(&s_rtcWdt);
    wdt_hal_write_protect_enable(&s_rtcWdt);
}

static void RtcWdtFeed(void)
{
    wdt_hal_write_protect_disable(&s_rtcWdt);
    wdt_hal_feed(&s_rtcWdt);
    wdt_hal_write_protect_enable(&s_rtcWdt);
}

static void HangWatchdogTask(void *pvParameter)
{
    (void)pvParameter;

    RtcWdtStart();

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(CHECK_PERIOD_MS));

        int64_t now = esp_timer_get_time();

        if (now - s_clrFedUs > HANG_TIMEOUT_US)
        {
            static char reason[384];
            int n = snprintf(reason, sizeof(reason), "LEDTREES hang watchdog: CLR stalled: ");
            NF_HangWatchdog_DescribeClr(reason + n, sizeof(reason) - n);
            esp_system_abort(reason);
        }

        // lwIP comes up when the network is initialised; until the first probe
        // has run there is nothing to watch. A full mailbox just skips a post.
        if (s_tcpipSeen && now - s_tcpipRanUs > HANG_TIMEOUT_US)
        {
            esp_system_abort("LEDTREES hang watchdog: tcpip stalled");
        }

        // the tcpip mailbox exists only once its thread has been created
        if (s_tcpipSeen || xTaskGetHandle(TCPIP_THREAD_NAME) != NULL)
        {
            (void)tcpip_try_callback(TcpipProbe, NULL);
        }

        RtcWdtFeed();
    }
}

void NF_HangWatchdog_Start(void)
{
    s_clrFedUs = esp_timer_get_time();

    // Core 0, above the CLR, the receiver and the frame feed, below the
    // Wi-Fi and tcpip tasks: it only has to run when everything else is blocked.
    xTaskCreatePinnedToCore(&HangWatchdogTask, "HangWatchdog", 3072, NULL, 10, NULL, 0);
}

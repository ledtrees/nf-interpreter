//-----------------------------------------------------------------------------
//
// LEDTREES: adapter from the Utilities stubs to the ledtrees_sysinfo component.
//
// Identification, CRC, the SD bus probe, the reset reason, the heap watermark
// and handing out the core dump all live in a separate IDF component - the
// ledtrees-idf-components repository, components/ledtrees_sysinfo. What is left
// here is unwrapping the managed arrays and validating their sizes.
//
// The EXCEPTIONS depend on nf-interpreter itself, so they are not moved into the
// component and their implementation stays here in full:
// - NativeWifiReconnect uses the network module (NF_ESP32_IsToConnect from
//   NF_ESP32_Network.h);
// - NativeSdFormatForeign formats through FatFs, which this repository configures
//   with its own ffconf.h (targets/ESP32/<board>/ffconf.h); the include path to it
//   is given to the FatFs target only, so the component cannot see it. Reading and
//   classifying the card is still the component's job.
//
//-----------------------------------------------------------------------------

#include "interoplib.h"
#include "interoplib_interoplib_Utilities.h"

#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <ff.h>
#include <ledtrees_sysinfo.h>
#include <sdmmc_cmd.h>
#include <string.h>

// NF_ESP32_IsToConnect is the auto-reconnect flag of the network module
// (NF_ESP32_Wireless.cpp): while it is set, the WIFI_EVENT_STA_DISCONNECTED
// handler calls esp_wifi_connect on its own - see NativeWifiReconnect. Declared
// through the header rather than a local extern, since a local copy could drift
// away from the real type.
#include <NF_ESP32_Network.h>

using namespace interoplib::interoplib;

// A forced Wi-Fi STA reconnect, the cure for a "dead link" the driver does not
// notice: the node believes it is still connected to the PREVIOUS instance of
// the AP (MAIN rebooted faster than the beacon timeout, or that timeout never
// fired) and keeps sending frames with stale keys into nowhere. The managed
// watchdog (ScreenCommandService) calls this method when MAIN goes quiet.
//
// esp_wifi_disconnect plus an unconditional esp_wifi_connect. A disconnect
// alone is not enough: the driver only raises STA_DISCONNECTED (handled in
// targetHAL_Network.cpp, which calls esp_wifi_connect itself while
// NF_ESP32_IsToConnect is set) from the connected or connecting state. In idle -
// where the auto-reconnect chain has broken, since the result of
// esp_wifi_connect in the handler is not checked and a single collision with a
// scan breaks the chain for good - a disconnect produces no event, and without a
// direct connect the node would stay offline until the power is cycled. The
// direct call is harmless in the other states too: connected or connecting
// returns an error, and after a disconnect the event handler does the reconnect.
// The flag is set explicitly rather than relying on its current value: the call
// only makes sense on an STA device, where a connection is expected in normal
// operation. Errors are not raised through hr: retrying in any driver state is
// harmless, and the watchdog will repeat on its own interval anyway.
void Utilities::NativeWifiReconnect( HRESULT &hr )
{
    hr = S_OK;

    // Gate on the mode: a reconnect only makes sense where there is an STA
    // interface. On an AP device (MAIN, or the unconfigured setup mode) the call
    // is a no-op: esp_wifi_disconnect would return an error there anyway, but
    // what matters is leaving NF_ESP32_IsToConnect alone, since for an AP
    // configuration the flag must stay false. If Wi-Fi is not initialised,
    // esp_wifi_get_mode returns an error, which is also a no-op.
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA))
    {
        return;
    }

    // The flag is set explicitly rather than relying on its current value: on an
    // STA device it is already true (set by the normal connect with AutoConnect),
    // so the assignment is idempotent, but it guards a call made in the window
    // before that connect has happened.
    NF_ESP32_IsToConnect = true;
    esp_wifi_disconnect();
    esp_wifi_connect();
}

void Utilities::NativeGetBaseMac( CLR_RT_TypedArray_UINT8 param0, HRESULT &hr )
{
    // managed must pass a buffer of at least 6 bytes: anything shorter, or NULL,
    // would be a write past the end
    if (param0.GetBuffer() == NULL || param0.GetSize() < 6) {
        hr = CLR_E_INVALID_PARAMETER;
        return;
    }

    lt_sys_base_mac(param0.GetBuffer());
}

// An incremental zlib compatible CRC32: a table driven byte loop on nanoCLR
// takes tens of seconds to checksum a megabyte-sized .ltf, the ROM function
// takes milliseconds.
unsigned int Utilities::NativeCrc32( unsigned int param0, CLR_RT_TypedArray_UINT8 param1, signed int param2, signed int param3, HRESULT &hr )
{
    const uint8_t *data = (const uint8_t *)param1.GetBuffer();
    signed int offset = param2;
    signed int count = param3;

    // The bounds of the managed array are checked here: the CLR bounds check does
    // not apply to native code. offset and count are added as unsigned - for
    // signed int the sum of two large positive values overflows, which is
    // undefined behaviour and lets the compiler drop the check altogether.
    if (data == NULL || offset < 0 || count < 0 ||
        (uint32_t)offset + (uint32_t)count > param1.GetSize())
    {
        hr = CLR_E_INVALID_PARAMETER;
        return param0;
    }

    return lt_sys_crc32(param0, data + offset, (size_t)count);
}

// A diagnostic probe of the SD bus (see Storage.Init on the managed side):
// 0 ok, 1 timeout (no response), 2 CRC or data, 3 anything else.
signed int Utilities::NativeSdProbe( uint8_t width, uint16_t freqKhz, CLR_RT_TypedArray_UINT8 pins, HRESULT &hr )
{
    hr = S_OK;

    if (pins.GetSize() < 6) {
        hr = CLR_E_INVALID_PARAMETER;
        return LT_SD_OTHER;
    }

    signed int result = lt_sys_sd_probe(width, freqKhz, (const uint8_t *)pins.GetBuffer());

    if (result == LT_SD_PARAM) {
        hr = CLR_E_INVALID_PARAMETER;
        return LT_SD_OTHER;
    }

    return result;
}

// The reason for the last reset, the raw esp_reset_reason_t value (0 unknown,
// 1 poweron, 2 external, 3 software, 4 panic, 5 int wdt, 6 task wdt, 7 wdt,
// 8 deep sleep, 9 brownout, 10 sdio). The managed side repeats exactly these
// numbers (TelemetryEventCodes.ResetReason): a translation table here would only
// add one more place for the two enumerations to drift apart when IDF is
// updated.
uint8_t Utilities::NativeGetResetReason( HRESULT &hr )
{
    hr = S_OK;
    return lt_sys_reset_reason();
}

// Watermark: the minimum free memory over the whole uptime. The current "free"
// answers whether there is enough right now, this one answers whether we ever
// came close to the edge.
unsigned int Utilities::NativeGetMinFreeHeap( bool spiRam, HRESULT &hr )
{
    hr = S_OK;
    return lt_sys_min_free_heap(spiRam);
}

// ---------------------------------------------------------------------------
// The core dump of a native panic (the coredump partition, docs/telemetry.md).
//
// The managed log ring (LogRing) does not survive a panic: by then the CLR is no
// longer executing, and the stack of the task that crashed is visible ONLY from
// here.
// ---------------------------------------------------------------------------

unsigned int Utilities::NativeGetCoredumpSize( HRESULT &hr )
{
    hr = S_OK;
    return lt_sys_coredump_size();
}

signed int Utilities::NativeReadCoredump( unsigned int offset, CLR_RT_TypedArray_UINT8 buffer, signed int count, HRESULT &hr )
{
    hr = S_OK;

    // The bounds of the managed array are checked here: the CLR bounds check does
    // not apply to native code (the same reasoning as in NativeCrc32).
    if (buffer.GetBuffer() == NULL || count < 0 || (uint32_t)count > buffer.GetSize())
    {
        hr = CLR_E_INVALID_PARAMETER;
        return 0;
    }

    return lt_sys_coredump_read(offset, buffer.GetBuffer(), (size_t)count);
}

bool Utilities::NativeEraseCoredump( HRESULT &hr )
{
    hr = S_OK;
    return lt_sys_coredump_erase();
}

// ---------------------------------------------------------------------------
// Formatting a card with a foreign layout (Storage.Init on the managed side).
// ---------------------------------------------------------------------------

// What was done to the card - the second byte of the result, the managed
// SdFormat* constants.
#define SD_FORMAT_SKIPPED 0
#define SD_FORMAT_DONE 1
#define SD_FORMAT_FAILED 2
#define SD_FORMAT_DONE_SMALL 3 // too small for FAT32 - FatFs made FAT12/16

// The allocation unit is the 16 KB the nanoFramework mount uses
// (Target_System_IO_FileSystem.c). The work buffer sets how many sectors f_mkfs
// writes per command when it clears the FAT area - tens of MB on a large card,
// so the 4 KB minimum would turn seconds into minutes. DMA capable, otherwise the
// SDMMC driver bounces it through its own buffer sector by sector.
#define SD_FORMAT_ALLOC_UNIT (16 * 1024)
#define SD_FORMAT_WORK_BUFFER (16 * 1024)

static const char *SD_FORMAT_TAG = "sd_format";

// Formats the card as FAT32 (FM_FAT32 rather than the FM_ANY of
// esp_vfs_fat_sdmmc_mount) so that an interrupted format is redone on the next
// boot instead of leaving a card that mounts with garbage in its FAT.
//
// f_mkfs writes the volume boot sector FIRST, then clears the FAT and the root
// directory, and only at the very end writes the partition table (create_partition,
// when the volume is a new single partition). IDF's partition_card runs f_fdisk
// before it, so the MBR already points at the new volume while its FAT is being
// cleared: a reset or power loss in that window - the monitor opening the
// USB-Serial-JTAG port resets the chip - leaves a FAT32 that mounts and is
// corrupt, and being FAT it is never formatted again. Here sector 0 is zeroed
// instead, which makes the partition table written by f_mkfs the commit point:
// until it lands, the card classifies as blank and is formatted again.
//
// Returns SD_FORMAT_DONE, SD_FORMAT_DONE_SMALL or SD_FORMAT_FAILED.
static int32_t FormatFat(sdmmc_card_t *card)
{
    BYTE pdrv = 0xFF;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == 0xFF)
    {
        ESP_LOGE(SD_FORMAT_TAG, "no free FatFs drive");
        return SD_FORMAT_FAILED;
    }

    size_t workSize = SD_FORMAT_WORK_BUFFER;
    void *work = heap_caps_malloc(workSize, MALLOC_CAP_DMA);
    if (work == NULL)
    {
        workSize = FF_MAX_SS;
        work = heap_caps_malloc(workSize, MALLOC_CAP_DMA);
    }
    if (work == NULL)
    {
        ESP_LOGE(SD_FORMAT_TAG, "no memory for the work buffer");
        return SD_FORMAT_FAILED;
    }

    // the first destructive write: from here until f_mkfs writes the partition
    // table the card reads as blank
    memset(work, 0, (size_t)card->csd.sector_size);
    if (sdmmc_write_sectors(card, work, 0, 1) != ESP_OK)
    {
        ESP_LOGE(SD_FORMAT_TAG, "clearing sector 0 failed");
        heap_caps_free(work);
        return SD_FORMAT_FAILED;
    }

    ff_diskio_register_sdmmc(pdrv, card);

    char drv[3] = {(char)('0' + pdrv), ':', 0};
    bool small = false;
    MKFS_PARM fat32 = {FM_FAT32, 2, 0, 0, SD_FORMAT_ALLOC_UNIT};
    FRESULT res = f_mkfs(drv, &fat32, work, workSize);

    // too few clusters for FAT32 on a small card: let FatFs pick FAT12/16 and the
    // cluster size itself. f_mkfs gives up before its first write, so sector 0 is
    // still clear for the second attempt.
    if (res == FR_MKFS_ABORTED)
    {
        MKFS_PARM fat = {FM_FAT, 2, 0, 0, 0};
        res = f_mkfs(drv, &fat, work, workSize);
        small = true;
    }

    ff_diskio_unregister(pdrv);
    heap_caps_free(work);

    ESP_LOGW(SD_FORMAT_TAG, "format -> %d", (int)res);
    if (res != FR_OK)
        return SD_FORMAT_FAILED;
    return small ? SD_FORMAT_DONE_SMALL : SD_FORMAT_DONE;
}

// Result: the low byte is LT_SD_FS_* (what was found on the card), the second
// byte is SD_FORMAT_*. Classifying and formatting happen in one session with the
// card: re-initialising it in between would let a different card state - or a
// different card - be formatted on a decision made about the previous one.
signed int Utilities::NativeSdFormatForeign( uint8_t width, uint16_t freqKhz, CLR_RT_TypedArray_UINT8 pins, HRESULT &hr )
{
    hr = S_OK;

    if (pins.GetSize() < 6) {
        hr = CLR_E_INVALID_PARAMETER;
        return LT_SD_FS_UNREADABLE;
    }

    sdmmc_card_t *card = NULL;
    int32_t opened = lt_sys_sd_open(width, freqKhz, (const uint8_t *)pins.GetBuffer(), &card);
    if (opened == LT_SD_PARAM) {
        hr = CLR_E_INVALID_PARAMETER;
        return LT_SD_FS_UNREADABLE;
    }
    if (opened != LT_SD_OK) {
        return LT_SD_FS_UNREADABLE;
    }

    int32_t kind = lt_sys_sd_classify(card);
    int32_t outcome = SD_FORMAT_SKIPPED;
    if (LT_SD_FS_IS_FOREIGN(kind)) {
        outcome = FormatFat(card);
    }

    lt_sys_sd_close(card);
    return kind | (outcome << 8);
}

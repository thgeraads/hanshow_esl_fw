#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw_213.h"
#include "epd_bwr_213.h"
#include "epd_bw_213_ice.h"
#include "epd_bwr_154.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "battery.h"

#include "OneBitDisplay.h"
#include "TIFF_G4.h"
extern const uint8_t ucMirror[];
#include "Roboto_Black_80.h"
#include "font_60.h"
#include "font16.h"
#include "font30.h"

RAM uint8_t epd_model = 0; // 0 = Undetected, 1 = BW213, 2 = BWR213, 3 = BWR154, 4 = BW213ICE
const char *epd_model_string[] = {"NC", "BW213", "BWR213", "BWR154", "213ICE"};
RAM uint8_t epd_update_state = 0;

const char *BLE_conn_string[] = {"", "B"};
RAM uint8_t epd_temperature_is_read = 0;
RAM uint8_t epd_temperature = 0;

uint8_t epd_buffer[epd_buffer_size];
uint8_t epd_temp[epd_buffer_size]; // for OneBitDisplay to draw into
OBDISP obd;                        // virtual display structure
TIFFIMAGE tiff;

// With this we can force a display if it wasnt detected correctly
void set_EPD_model(uint8_t model_nr)
{
    epd_model = model_nr;
}

// Here we detect what E-Paper display is connected
_attribute_ram_code_ void EPD_detect_model(void)
{
    EPD_init();
    // system power
    EPD_POWER_ON();

    WaitMs(10);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    // Here we neeed to detect it
    if (EPD_BWR_213_detect())
    {
        epd_model = 3;
    }
    else if (EPD_BWR_154_detect())// Right now this will never trigger, the 154 is same to 213BWR right now.
    {
        epd_model = 3;
    }
    else if (EPD_BW_213_ice_detect())
    {
        epd_model = 3;
    }
    else
    {
        epd_model = 3;
    }

    EPD_POWER_OFF();
}

_attribute_ram_code_ int8_t EPD_read_temp(void)
{
    if (epd_temperature_is_read)
        return epd_temperature;

    if (!epd_model)
        EPD_detect_model();

    EPD_init();
    // system power
    EPD_POWER_ON();
    WaitMs(5);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    if (epd_model == 1)
        epd_temperature = EPD_BW_213_read_temp();
    else if (epd_model == 2)
        epd_temperature = EPD_BWR_213_read_temp();
    else if (epd_model == 3)
        epd_temperature = EPD_BWR_154_read_temp();
    else if (epd_model == 4)
        epd_temperature = EPD_BW_213_ice_read_temp();

    EPD_POWER_OFF();

    epd_temperature_is_read = 1;

    return epd_temperature;
}

_attribute_ram_code_ void EPD_Display(unsigned char *image, int size, uint8_t full_or_partial)
{
    if (!epd_model)
        EPD_detect_model();

    EPD_init();
    // system power
    EPD_POWER_ON();
    WaitMs(5);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    if (epd_model == 1)
        epd_temperature = EPD_BW_213_Display(image, size, full_or_partial);
    else if (epd_model == 2)
        epd_temperature = EPD_BWR_213_Display(image, size, full_or_partial);
    else if (epd_model == 3)
        epd_temperature = EPD_BWR_154_Display(image, size, full_or_partial);
    else if (epd_model == 4)
        epd_temperature = EPD_BW_213_ice_Display(image, size, full_or_partial);

    epd_temperature_is_read = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    if (!epd_model)
        EPD_detect_model();

    if (epd_model == 1)
        EPD_BW_213_set_sleep();
    else if (epd_model == 2)
        EPD_BWR_213_set_sleep();
    else if (epd_model == 3)
        EPD_BWR_154_set_sleep();
    else if (epd_model == 4)
        EPD_BW_213_ice_set_sleep();

    EPD_POWER_OFF();
    epd_update_state = 0;
}

_attribute_ram_code_ uint8_t epd_state_handler(void)
{
    switch (epd_update_state)
    {
    case 0:
        // Nothing todo
        break;
    case 1: // check if refresh is done and sleep epd if so
        if (epd_model == 1)
        {
            if (!EPD_IS_BUSY())
                epd_set_sleep();
        }
        else
        {
            if (EPD_IS_BUSY())
                epd_set_sleep();
        }
        break;
    }
    return epd_update_state;
}

_attribute_ram_code_ void FixBuffer(uint8_t *pSrc, uint8_t *pDst, uint16_t width, uint16_t height)
{
    int x, y;
    uint8_t *s, *d;
    for (y = 0; y < (height / 8); y++)
    { // byte rows
        d = &pDst[y];
        s = &pSrc[y * width];
        for (x = 0; x < width; x++)
        {
            d[x * (height / 8)] = ~ucMirror[s[width - 1 - x]]; // invert and flip
        }                                                      // for x
    }                                                          // for y
}

_attribute_ram_code_ void TIFFDraw(TIFFDRAW *pDraw)
{
    uint8_t uc = 0, ucSrcMask, ucDstMask, *s, *d;
    int x, y;

    s = pDraw->pPixels;
    y = pDraw->y;
    d = &epd_buffer[(200 * (y / 8))]; // Adjusted to start at correct line
    ucDstMask = 0x80 >> (y & 7); // destination mask

    for (x = 0; x < pDraw->iWidth; x++)
    {
        if (ucSrcMask == 0)
        {
            ucSrcMask = 0x80;
            uc = *s++;
        }
        if (!(uc & ucSrcMask))
        {
            // Instead of d[-(x * 16)], adjust for positive index
            d[x / 8] &= ~ucDstMask;
        }
        ucSrcMask >>= 1;
    }
}


_attribute_ram_code_ void epd_display_tiff(uint8_t *pData, int iSize)
{
    // test G4 decoder
    memset(epd_buffer, 0xff, epd_buffer_size); // clear to white
    TIFF_openRAW(&tiff, 250, 122, BITDIR_MSB_FIRST, pData, iSize, TIFFDraw);
    TIFF_setDrawParameters(&tiff, 65536, TIFF_PIXEL_1BPP, 0, 0, 250, 122, NULL);
    TIFF_decode(&tiff);
    TIFF_close(&tiff);
    EPD_Display(epd_buffer, epd_buffer_size, 1);
}

extern uint8_t mac_public[6];
_attribute_ram_code_ void epd_display(uint32_t time_is, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    if (epd_update_state)
        return;

    if (!epd_model)
    {
        EPD_detect_model();
    }
    uint16_t resolution_w = 250;
    uint16_t resolution_h = 128; // 122 real pixel, but needed to have a full byte
    if (epd_model == 1)
    {
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 2)
    {
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 3)
    {
        resolution_w = 200;
        resolution_h = 200;
    }
    else if (epd_model == 4)
    {
        resolution_w = 250;
        resolution_h = 128;
    }

    obdCreateVirtualDisplay(&obd, resolution_w, resolution_h, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    char buff[100];
    sprintf(buff, "ESL_%02X%02X%02X %s", mac_public[2], mac_public[1], mac_public[0], epd_model_string[epd_model]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 1, 17, (char *)buff, 1);
    sprintf(buff, "%s", BLE_conn_string[ble_get_connected()]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 232, 20, (char *)buff, 1);
    sprintf(buff, "%02d:%02d", ((time_is / 60) / 60) % 24, (time_is / 60) % 60);
    obdWriteStringCustom(&obd, (GFXfont *)&DSEG14_Classic_Mini_Regular_40, 50, 65, (char *)buff, 1);
    sprintf(buff, "%d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Special_Elite_Regular_30, 10, 95, (char *)buff, 1);
    sprintf(buff, "Battery %dmV", battery_mv);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 10, 120, (char *)buff, 1);
    FixBuffer(epd_temp, epd_buffer, resolution_w, resolution_h);
    EPD_Display(epd_buffer, resolution_w * resolution_h / 8, full_or_partial);
}

_attribute_ram_code_ void epd_display_char(uint8_t data)
{
    int i;
    for (i = 0; i < epd_buffer_size; i++)
    {
        epd_buffer[i] = data;
    }
    EPD_Display(epd_buffer, epd_buffer_size, 1);
}

// Helper function to convert a single hex character to its integer value
uint8_t hex_to_int(char hex) {
    if (hex >= '0' && hex <= '9')
        return hex - '0';
    if (hex >= 'A' && hex <= 'F')
        return hex - 'A' + 10;
    if (hex >= 'a' && hex <= 'f')
        return hex - 'a' + 10;
    return 0; // default for invalid input
}

// Function to load a hex string into the e-paper display buffer
void load_bitmap_to_epd_buffer(const char* bitmap_hex) {
    int i;
    for (i = 0; i < epd_buffer_size; i++) {
        // Each byte in `epd_buffer` is represented by two hex characters
        uint8_t high_nibble = hex_to_int(bitmap_hex[i * 2]);
        uint8_t low_nibble = hex_to_int(bitmap_hex[i * 2 + 1]);
        epd_buffer[i] = (high_nibble << 4) | low_nibble;
    }
}

void display_bitmap(char* hexBitmap, uint8_t partial) {
    // compare hexBitmap to predefined string %boot%
    if (strcmp(hexBitmap, "%boot%") == 0) {
        hexBitmap = "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF00000000000000000000FFE00000000000000000003FC000FF00000000000000000007FFF80000000000000000003FC000FF0000000000000000000FFFFE0000000000000000003FC000FF0000000000000000003FFFFF0000000000000000003FC000FF0000000000000000007FFFFF8000000000000000003FC000FF000000000000000000FFFFFFC000000000000000003FC000FF000000000000000001FFFFFFE000000000000000003FC000FF000000000000000001FFFFFFF000000000000000003FC000FF000000000000000003FFFFFFF000000000000000003FC000FF000000000000000003FFFFFFF800000000000000003FC000FF000000000000000007FFFFFFF800000000000000003FC000FF000000000000000007FFFFFFF800000000000000003FC000FF000000000000000007FFFFFFFC00000000000000003FC000FF00000000000000000FFFFFFFFC00000000000000003FC000FF00000000000000000FFFFFFFFC00000000000000003FC000FF00000000000000000FFFFFFFFC00000000000000003FC000FF00000000000000000FFFFFFFFC00000000000000003FC000FF00000000000000000FFFFFFFFC00000000000000003FC000FF00000000000000000FFFFFFFFC00000000000000003FC000FF00000000000000000FFFFFFFFC00000000000000003FC000FF00000000000000000FFFFFFFFC00000000000000003FC000FF000000000000000007FFFFFFFC00000000000000003FC000FF000000000000000007FFFFFFF800000000000000003FC000FF000000000000000007FFFFFFF800000000000000003FC000FF000000000000000003FFFFFFF000000000000000003FC000FF000000000000000001FFFFFFF000000000000000003FC000FF000000000000000001FFFFFFE000000000000000003FC000FF000000000000000000FFFFFFC000000000000000003FC000FF0000000000000000007FFFFF8000000000000000003FC000FF0000000000000000003FFFFF0000000000000000003FC000FF0000000000000000001FFFFE0000000000000000003FC000FF00000000000000000007FFFC0000000000000000003FC000FF00000000000000000001FFF00000000000000000003FC000FF000000000000000000003F000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF0000000000000000000000000000000000000000003FC000FF000000000000000000000000000000000C000000003FC000FF000000000000000000000000000000000C000000003FC000FF000000000000000000000000000000001E000000003FC000FF000000000000000000000000000000003F000000003FC000FF000000000000000000000000000000007F800000003FC000FF00000000000000000000000000000000FFC00000003FC000FF00000000000000000000000000000001FFE00000003FC000FF00000000000000000000000000000001FFE00000003FC000FF00000000000000000000000000000003FFF00000003FC000FF00000000000000000000000000000007FFF80000003FC000FF0000000000000000000000000000000FFFFC0000003FC000FF0000000000000000000000000000001FFFFE0000003FC000FF0000000000000000000000000000003FFFFF0000003FC000FF0000000000000000000000000000003FFFFF0000003FC000FF0000000000000000000000000000007FFFFF8000003FC000FF000000000000000000000000000000FFFFFFC000003FC000FF000000000000000000000000000001FFFFFFE000003FC000FF000000000000000000000000000003FFFFFFF000003FC000FF000000000000000000000000000007FFFFFFF000003FC000FF000000000000000000000000000007FFFFFFF800003FC000FF00000000000000000000000000000FFFFFFFFC00003FC000FF00000000000000000000000000001FFFFFFFFE00003FC000FF00000000000000000000000000003FFFFFFFFF00003FC000FF00000000000000000000000000007FFFFFFFFF80003FC000FF0000000000000000000000000000FFFFFFFFFF80003FC000FF0000000000000000000000000000FFFFFFFFFFC0003FC000FF0000000000000000000000000001FFFFFFFFFFE0003FC000FF0000000000000000000000000003FFFFFFFFFFF0003FC000FF0000000000000000000000000007FFFFFFFFFFF8003FC000FF000000000000000000000000000FFFFFFFFFFFFC003FC000FF000000000000000000000000000FFFFFFFFFFFFC003FC000FF000000000000000000000000001FFFFFFFFFFFFE003FC000FF000000000000000000000000003FFFFFFFFFFFFF003FC000FF000000000000000000000000007FFFFFFFFFFFFF803FC000FF00000000000000000000000000FFFFFFFFFFFFFFC03FC000FF00010000000000000000000001FFFFFFFFFFFFFFE03FC000FF00038000000000000000000001FFFFFFFFFFFFFFE03FC000FF0003C000000000000000000003FFFFFFFFFFFFFFF03FC000FF0007E000000000000000000007FFFFFFFFFFFFFFF83FC000FF000FF00000000000000000000FFFFFFFFFFFFFFFFC3FC000FF001FF80000000000000000001FFFFFFFFFFFFFFFFE3FC000FF003FF80000000000000000003FFFFFFFFFFFFFFFFF3FC000FF007FFC0000000000000000003FFFFFFFFFFFFFFFFF3FC000FF00FFFE0000000000000000007FFFFFFFFFFFFFFFFFBFC000FF01FFFF000000000000000000FFFFFFFFFFFFFFFFFFFFC000FF03FFFF800000000000000001FFFFFFFFFFFFFFFFFFFFC000FF07FFFFC00000000000000003FFFFFFFFFFFFFFFFFFFFC000FF0FFFFFE00000000000000007FFFFFFFFFFFFFFFFFFFFC000FF1FFFFFF00000000000000007FFFFFFFFFFFFFFFFFFFFC000FF3FFFFFF8000000000000000FFFFFFFFFFFFFFFFFFFFFC000FF7FFFFFFC000000000000001FFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFE000000000000003FFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFF000000000000007FFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFF80000000000000FFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFC0000000000000FFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFE0000000000001FFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFE0000000000003FFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFF0000000000007FFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFF800000000000FFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFC00000000001FFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFE00000000001FFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFF00000000003FFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFF80000000007FFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFC000000000FFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFE000000001FFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFF000000001FFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFF800000003FFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFC00000007FFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFE0000000FFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFE0000001FFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFF0000003FFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFF8000003FFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFC000007FFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFE00000FFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFF00001FFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFF80003FFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFC0007FFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFE0007FFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFF000FFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFF801FFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFC03FFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFE07FFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFF0FFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFF0FFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFF9FFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"; // truncated for brevity
        partial = 0;
    }

    load_bitmap_to_epd_buffer(hexBitmap);

    // Display the bitmap
    EPD_Display(epd_buffer, epd_buffer_size, partial); // 1 for full refresh
}

_attribute_ram_code_ void epd_clear(void)
{
    memset(epd_buffer, 0x00, epd_buffer_size);
}

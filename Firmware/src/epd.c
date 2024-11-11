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
        epd_model = 2;
    }
    else if (EPD_BWR_154_detect())// Right now this will never trigger, the 154 is same to 213BWR right now.
    {
        epd_model = 3;
    }
    else if (EPD_BW_213_ice_detect())
    {
        epd_model = 4;
    }
    else
    {
        epd_model = 1;
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
    y = pDraw->y;                          // current line
    d = &epd_buffer[(249 * 16) + (y / 8)]; // rotated 90 deg clockwise
    ucDstMask = 0x80 >> (y & 7);           // destination mask
    ucSrcMask = 0;                         // src mask
    for (x = 0; x < pDraw->iWidth; x++)
    {
        // Slower to draw this way, but it allows us to use a single buffer
        // instead of drawing and then converting the pixels to be the EPD format
        if (ucSrcMask == 0)
        { // load next source byte
            ucSrcMask = 0x80;
            uc = *s++;
        }
        if (!(uc & ucSrcMask))
        { // black pixel
            d[-(x * 16)] &= ~ucDstMask;
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
        hexBitmap = "FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFF7FFFFFFFFFFFFFFFFFFFFF000000FFF0001FFFFFFFFFFFFFFFFFFF000000FFE000007FFFFFFFFFFFFFFFFF000000FFC0000007FFFFFFFFFFFFFFFF000000FF800000007FFFFFFFFFFFFFFF000000FF8000000007FFFFFFFFFFFFFF000000FF80000000007FFFFFFFFFFFFF000000FF00000000000FFFFFFFFFFFFF000000FF800000000001FFFFFFFFFFFF000000FF8000000000001FFFFFFFFFFF000000FF80000000000003FFFFFFFFFF000000FFC00000000000007FFFFFFFFF000000FFE00000000000000FFFFFFFFF000000FFF00000000000000007FFFFFF000000FFFC0000000000000000FFFFFF000000FFFFFFC00000000000003FFFFF000000FFFFFFFF00000000000007FFFF000000FFFFFFFFF0000000000003FFFF000000FFFFFFFFFE000000000001FFFF000000FFFFFFFFFFC00000000000FFFF000000FFFFFFFFFFFE0000000000FFFF000000FFFFFFFFFFFFC000000000FFFF000000FFFFFFFFFFFFFC00000000FFFF000000FFFFFFFFFFFFFF800000007FFF000000FFFFFFFFFFFFFFE00000007FFF000000FFFFFFFFFFFFFFFC0000003FFF000000FFFFFFFFFFFFFFFF8000003FFF000000FFFFFFFFFFFFFFFFFFE0003FFF000000FFFFFFFFFFFFFFFFFFE0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFFFFFFFFFFFFF0003FFF000000FFFFFFFF83FFFFFFFFF0003FFF000000FFFFFFF8001FFFFFFFF0003FFF000000FFFFFFF00001FFFFFFE0003FFF000000FFFFFFE000007FFFFFE0003FFF000000FFFFFFC000001FFFFFE0007FFF000000FFFFFF80000007FFFFE0007FFF000000FFFFFF80000001FFFFE0007FFF000000FFFFFF00000000FFFFE0007FFF000000FFFFFF000000003FFFE0007FFF000000FFFFFE000000000FFFC0007FFF000000FFFFFE0000000003FFC0007FFF000000FFFFFE00000000007F00007FFF000000FFFFFE0000000000000000FFFF000000FFFFFC0000000000000000FFFF000000FFFFFC0000000000000001FFFF000000FFFFFC000F800000000001FFFF000000FFFFFC000FF00000000003FFFF000000FFFFFC000FFC0000000003FFFF000000FFFFFC000FFF0000000007FFFF000000FFFFFC0007FF800000000FFFFF000000FFFFFC0007FFC00000001FFFFF000000FFFFFE0003FFF00000003FFFFF000000FFFFFE0003FFFC0000007FFFFF000000FFFFFE0001FFFF800000FFFFFF000000FFFFFE0001FFFFF00003FFFFFF000000FFFFFF0001FFFFF8000FFFFFFF000000FFFFFF0000FFFFFFFFFFFFFFFF000000FFFFFF8000FFFFFFFFFFFFFFFF000000FFFFFF80007FFFFFFFFFFFFFFF000000FFFFFFC0003FFFFFFFFFFFFFFF000000FFFFFFC0001FFFFFFFFFFFFFFF000000FFFFFFC0000FFFFFFFFFFFFFFF000000FFFFFFE00007FFFFFFFFFFFFFF000000FFFFFFE00003FFFFFFFFFFFFFF000000FFFFFFF00003FFFFFFFFFFFFFF000000FFFFFFF80000FFFFFFFFFFFFFF000000FFFFFFFC000007FFFFFFFFFFFF000000FFFFFFFE0000001FFFFFFFFFFF000000FFFFFFFE00000001FFFFFFFFFF000000FFFFFFFF000000007FFFFFFFFF000000FFFFFFFF800000000FFFFFFFFF000000FFFFFFFF8000000001FFFFFFFF000000FFFFFFFFC000000000FFFFFFFF000000FFFFFFFFE0000000007FFFFFFF000000FFFFFFFFF0000000003FFFFFFF000000FFFFFFFFF8000000001FFFFFFF000000FFFFFFFFFE000000000FFFFFFF000000FFFFFFFFFF8000000007FFFFFF000000FFFFFFFFFFE000000003FFFFFF000000FFFFFFFFFFFC00000003FFFFFF000000FFFFFFFFFFFFE0000001FFFFFF000000FFFFFFFFFFFFFF800001FFFFFF000000FFFFFFFFFFFFFFF00001FFFFFF000000FFFFFFFFFFFFFFFC0001FFFFFF000000FFFFFFFFFFFFFFFF0001FFFFFF000000FFFFFFFFFFFFFFFF8000FFFFFF000000FFFFFFFFFFFFFFFF8000FFFFFF000000FFFFFFFFFFFFFFFF8000FFFFFF000000FFFFFFFFFFFFFFFFC000FFFFFF000000FFFFFFFFFFFFFFFFC000FFFFFF000000FFFFFFFFFFFFFFFFC0007FFFFF000000FFFFFFFFFFFFFFFFC0007FFFFF000000FFFFFFFFFFFFFFFFE0007FFFFF000000FFFFFFFFFFFFFFFFE0007FFFFF000000FFFFFFFFFFFFFFFFE0007FFFFF000000FFFFFFFFFFFFFFFFE0007FFFFF000000FFFFFFFFFFFFFFFFE0007FFFFF000000FFFFFFFFFFFFFFFFE0007FFFFF000000FFFFFFFFFFFFFFFFE0007FFFFF000000FFFFFFFFFFFFFFFFE0007FFFFF000000FFFFFFFFFFFFC7FFC0007FFFFF000000FFFFFFFFFFFC000000007FFFFF000000FFFFFFFFFFE0000000007FFFFF000000FFFFFFFFFFC0000000007FFFFF000000FFFFFFFFFF80000000007FFFFF000000FFFFFFFFFF0000000000FFFFFF000000FFFFFFFFFE0000000001FFFFFF000000FFFFFFFFFC0000000003FFFFFF000000FFFFFFFFF80000000007FFFFFF000000FFFFFFFFF0000000000FFFFFFF000000FFFFFFFFF0000000001FFFFFFF000000FFFFFFFFE0000000003FFFFFFF000000FFFFFFFFC0000000007FFFFFFF000000FFFFFFFFC000000000FFFFFFFF000000FFFFFFFF8000000003FFFFFFFF000000FFFFFFFF80003FFFFFFFFFFFFF000000FFFFFFFF80007FFFFFFFFFFFFF000000FFFFFFFF8000FFFFFFFFFFFFFF000000FFFFFFFF0000FFFFFFFFFFFFFF000000FFFFFFFF0001FFFFFFFFFFFFFF000000FFFFFFFF0003FFFFFFFFFFFFFF000000FFFFFFFF0003FFFFFFFFFFFFFF000000FFFFFFFF0001FFFFFFFFFFFFFF000000FFFFFFFF0001FFFFFFFFFFFFFF000000FFFFFFFF8001FFFFFFFFFFFFFF000000FFFFFFFF8000FFFFFFFFFFFFFF000000FFFFFFFF8000FFFFFFFFFFFFFF000000FFFFFFFF80007FFFFFFFFFFFFF000000FFFFFFFFC0003FFFFFFFFFFFFF000000FFFFFFFFC00007FFFFFFFFFFFF000000FFFFFFFFC00000FFFFFFFFFFFF000000FFFFFFFFE000001FFFFFFFFFFF000000FFFFFFFFE0000003FFFFFFFFFF000000FFFFFFFFF0000000FFFFFFFFFF000000FFFFFFFFF80000007FFFFFFFFF000000FFFFFFFFFC0000003FFFFFFFFF000000FFFFFFFFFE0000001FFFFFFFFF000000FFFFFFFFFF0000001FFFFFFFFF000000FFFFFFFFFF8000000FFFFFFFFF000000FFFFFFFFFFC000000FFFFFFFFF000000FFFFFFFFFFE0000007FFFFFFFF000000FFFFFFFFFFF0000003FFFFFFFF000000FFFFFFFFFFFC000001FFFFFFFF000000FFFFFFFFFFFFC00001FFFFFFFF000000FFFFFFFFFFFFFC0001FFFFFFFF000000FFFFFFFFFFFFFF0001FFFFFFFF000000FFFFFFFFFFFFFF0001FFFFFFFF000000FFFFFFFFFFFFFF0001FFFFFFFF000000FFFFFFFFFFFFFE0001FFFFFFFF000000FFFFFFFFFFFFFC0001FFFFFFFF000000FFFFFFFFFFFFF80001FFFFFFFF000000FFFFFFFFFFFFF80001FFFFFFFF000000FFFFFFFFFFFFF00003FFFFFFFF000000FFFFFFFFFFFFE00007FFFFFFFF000000FFFFFFFFFFFFE0000FFFFFFFFF000000FFFFFFFFFFFFC0001FFFFFFFFF000000FFFFFFFFFFFF80003FFFFFFFFF000000FFFFFFFFFFFF80003FFFFFFFFF000000FFFFFFFFFFFF00007FFFFFFFFF000000FFFFFFFFFFFE0000FFFFFFFFFF000000FFFFFFFFFFFC0000FFFFFFFFFF000000FFFFFFFFFFF00001FFFFFFFFFF000000FFFFFFFFFFE00003FFFFFFFFFF000000FFFFFFFFFFC00007FFFFFFFFFF000000FFFFFFFFFF800007FFFFFFFFFF000000FFFFFFFFFF000007FFFFFFFFFF000000FFFFFFFFFE00000FFFFFFFFFFF000000FFFFFFFFFC00001FFFFFFFFFFF000000FFFFFFFFFC00003FFFFFFFFFFF000000FFFFFFFFFC0000FFFFFFFFFFFF000000FFFFFFFFF80001FFFFFFFFFFFF000000FFFFFFFFF80003FFFFFFFFFFFF000000FFFFFFFFF80001FFFFFFFFFFFF000000FFFFFFFFF800001FFFFFFFFFFF000000FFFFFFFFFC000003FFFFFFFFFF000000FFFFFFFFFC000001FFFFFFFFFF000000FFFFFFFFFC000000FFFFFFFFFF000000FFFFFFFFFE0000007FFFFFFFFF000000FFFFFFFFFF0000007FFFFFFFFF000000FFFFFFFFFF8000007FFFFFFFFF000000FFFFFFFFFFC000007FFFFFFFFF000000FFFFFFFFFFE000007FFFFFFFFF000000FFFFFFFFFFF000007FFFFFFFFF000000FFFFFFFFFFFC00007FFFFFFFFF000000FFFFFFFFFFFF00007FFFFFFFFF000000FFFFFFFFFFFF8000FFFFFFFFFF000000FFFFFFFFFFFFE001FFFFFFFFFF000000FFFFFFFFFFFFFE03FFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFFFFFFFFFFFFFFFFFF000000"; // truncated for brevity
    }

    load_bitmap_to_epd_buffer(hexBitmap);

    // Display the bitmap
    EPD_Display(epd_buffer, epd_buffer_size, partial); // 1 for full refresh
}

_attribute_ram_code_ void epd_clear(void)
{
    memset(epd_buffer, 0x00, epd_buffer_size);
}

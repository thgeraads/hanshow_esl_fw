#include "epd_ble_service.h"

#include <stdint.h>
#include "tl_common.h"
#include "stack/ble/ble.h"

#include "epd.h"
#include "ble.h"

extern uint8_t *epd_temp;

#define ASSERT_MIN_LEN(val, min_len) \
    if (val < min_len)               \
    {                                \
        return 0;                    \
    }

extern unsigned char epd_buffer[epd_buffer_size];
unsigned int byte_pos = 0;

// Define a buffer for accumulating the bitmap data
#define MAX_BITMAP_SIZE (epd_buffer_size * 2) // Adjust this size as needed
uint8_t bitmap_buffer[MAX_BITMAP_SIZE];
unsigned int bitmap_buffer_pos = 0;

// Forward declaration of the display function
void display_bitmap(char *hexBitmap, uint8_t partial);

// Function to handle BLE write requests
int epd_ble_handle_write(void *p)
{
    rf_packet_att_write_t *req = (rf_packet_att_write_t *)p;
    uint8_t *payload = &req->value;
    unsigned int payload_len = req->l2capLen - 3;

    ASSERT_MIN_LEN(payload_len, 1);

    switch (payload[0])
    {
        // Clear EPD display.
        case 0x00:
            ASSERT_MIN_LEN(payload_len, 2);
            memset(epd_buffer, payload[1], sizeof(epd_buffer));
            ble_set_connection_speed(40);
            return 0;

            // Push buffer to display.
        case 0x01:
            ble_set_connection_speed(200);
            EPD_Display(epd_buffer, epd_buffer_size, 1);
            return 0;

            // Set byte_pos.
        case 0x02:
            ASSERT_MIN_LEN(payload_len, 3);
            byte_pos = payload[1] << 8 | payload[2];
            return 0;

            // Write data to image buffer (accumulate data in bitmap_buffer).
        case 0x03:
            if (bitmap_buffer_pos + payload_len - 1 >= MAX_BITMAP_SIZE)
            {
                return 0; // Prevent buffer overflow
            }
            memcpy(bitmap_buffer + bitmap_buffer_pos, payload + 1, payload_len - 1);
            bitmap_buffer_pos += payload_len - 1;
            return 0;

            // Display the accumulated bitmap data.
        case 0x05:
            if (bitmap_buffer_pos == 0)
            {
                return 0; // No data received
            }
            bitmap_buffer[bitmap_buffer_pos] = '\0'; // Null-terminate the buffer
            display_bitmap((char *)bitmap_buffer, 1); // Use 1 for full refresh
            bitmap_buffer_pos = 0; // Reset the buffer position for the next transmission
            return 0;

            // Decode & display a TIFF image.
        case 0x04:
            epd_display_tiff(epd_buffer, byte_pos);
            return 0;

        default:
            return 0;
    }

    return 0;
}
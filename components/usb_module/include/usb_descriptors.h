#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include "tusb.h"

// ------------ Device Descriptor ------------
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A,
    .idProduct          = 0x1324,
    .bcdDevice          = 0x0101,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// ------------ HID Report Descriptors ------------
#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_NKRO 2
#define REPORT_ID_COMM 3

#define NKRO_KEYS 0xE7
#define NKRO_BYTES ((NKRO_KEYS + 7) / 8)

#define NKRO_REPORT_SIZE 64
#define COMM_REPORT_SIZE 48

// Keyboard report descriptor (6KRO + NKRO)
static uint8_t const desc_hid_report_kbd[] = {
    // 6KRO boot keyboard
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),

    // NKRO bitmap
    0x05, 0x01,                         // Usage Page (Generic Desktop)
    0x09, 0x06,                         // Usage (Keyboard)
    0xA1, 0x01,                         // Collection (Application)
    0x85, REPORT_ID_NKRO,               // Report ID
    0x05, 0x07,                         // Usage Page (Keyboard/Keypad)
    0x19, 0x00,                         // Usage Minimum (0)
    0x29, (NKRO_KEYS - 1),              // Usage Maximum
    0x15, 0x00,                         // Logical Minimum (0)
    0x25, 0x01,                         // Logical Maximum (1)
    0x75, 0x01,                         // Report Size (1)
    0x95, NKRO_KEYS,                    // Report Count
    0x81, 0x02,                         // Input (Data,Var,Abs)
    0x75, 0x01,                         // Report Size (1)
    0x95, (NKRO_BYTES*8 - NKRO_KEYS),   // Report Count (padding)
    0x81, 0x03,                         // Input (Const,Var,Abs)
    0xC0                                // End Collection
};

// Vendor-defined HID descriptor for bidirectional config data (64 bytes input + output)
static uint8_t const desc_hid_report_comm[] = {
    0x06, 0xFF, 0xFF,                  // Usage Page (Vendor Defined)
    0x09, 0x01,                        // Usage (Vendor Defined)
    0xA1, 0x01,                        // Collection (Application)
    0x85, REPORT_ID_COMM,              // Report ID
    0x09, 0x02,                        // Usage (Vendor Defined) - Input
    0x15, 0x00,                        // Logical Minimum (0)
    0x26, 0xFF, 0x00,                  // Logical Maximum (255)
    0x75, 0x08,                        // Report Size (8)
    0x95, COMM_REPORT_SIZE,            // Report Count
    0x81, 0x02,                        // Input (Data,Var,Abs)
    0x09, 0x03,                        // Usage (Vendor Defined) - Output
    0x15, 0x00,                        // Logical Minimum (0)
    0x26, 0xFF, 0x00,                  // Logical Maximum (255)
    0x75, 0x08,                        // Report Size (8)
    0x95, COMM_REPORT_SIZE,            // Report Count
    0x91, 0x02,                        // Output (Data,Var,Abs)
    0xC0                               // End Collection
};

uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance);

// ------------ Device and Endpoint Configuration -----------
enum {
    EPNUM_HID_KBD_IN = 0x81,
    EPNUM_HID_COMM_IN = 0x82,
    EPNUM_HID_COMM_OUT = 0x02,
};

enum {
    ITF_NUM_HID_KBD = 0,
    ITF_NUM_HID_COMM = 1,
    ITF_NUM_TOTAL = 2
};

// Keyboard report: 8 bytes boot + 1 extra + NKRO bytes = ~72 bytes
#define KBD_REPORT_LEN (8 + 1 + NKRO_BYTES)

// Configuration descriptor component sizes
#define CONFIG_DESC_SIZE        9       // TUD_CONFIG_DESCRIPTOR
#define HID_KBD_DESC_SIZE       25      // TUD_HID_DESCRIPTOR (Interface 9 + HID 9 + Endpoint 7)
#define HID_COMM_DESC_SIZE      32      // TUD_HID_INOUT_DESCRIPTOR (Interface 9 + HID 9 + Endpoint Out 7 + Endpoint In 7)

// Total configuration descriptor length
#define CONFIG_TOTAL_LEN        (CONFIG_DESC_SIZE + HID_KBD_DESC_SIZE + HID_COMM_DESC_SIZE)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,                              // bConfigurationValue
        ITF_NUM_TOTAL,                  // bNumInterfaces
        0,                              // iConfiguration (no string)
        CONFIG_TOTAL_LEN,               // wTotalLength
        TUSB_DESC_CONFIG_ATT_SELF_POWERED, // bmAttributes
        500                             // bMaxPower (in 2mA units = 1000mA)
    ),

    // Interface 0: HID Keyboard
    TUD_HID_DESCRIPTOR(
        ITF_NUM_HID_KBD,                // bInterfaceNumber
        0,                              // iInterface (no string)
        true,                           // protocol (1=Boot interface)
        sizeof(desc_hid_report_kbd),    // wDescriptorLength
        EPNUM_HID_KBD_IN,               // bEndpointAddress
        KBD_REPORT_LEN,                 // wMaxPacketSize
        1                              // bInterval (milliseconds)
    ),

    // Interface 1: HID Comm (bidirectional)
    TUD_HID_INOUT_DESCRIPTOR(
        ITF_NUM_HID_COMM,               // bInterfaceNumber
        0,                              // iInterface (no string)
        false,                          // protocol (0=no boot interface)
        sizeof(desc_hid_report_comm),   // wDescriptorLength
        EPNUM_HID_COMM_OUT,             // bEndpointAddress (OUT)
        EPNUM_HID_COMM_IN,              // bEndpointAddress (IN)
        COMM_REPORT_SIZE,               // wMaxPacketSize
        5                               // bInterval (milliseconds)
    )
};

// ------------ String Descriptors ------------
static char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04},         // 0: Language
    "Tecleados",                        // 1: Manufacturer
    "DF-ONE",                           // 2: Product
    "13548",                            // 3: Serial
    "Tecleados Deltafors Donaltron",    // 4: Keyboard interface
    "Tecleados Comms ITF"               // 5: Bulk COMM interface
};

#endif /* USB_DESCRIPTORS_H_ */
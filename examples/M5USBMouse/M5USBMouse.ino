#include "M5Unified.h"
#include "M5_Max3421E_Usb.h"

// Language ID: English
#define LANGUAGE_ID 0x0409

/*
  The board is equipped with two dip switches to adapt to different M5 series hosts.
  https://static-cdn.m5stack.com/resource/docs/products/module/USB%20v1.2%20Module/pinMap-70b8e2ad-8325-4887-af33-44e3dae91520.png
  If you need to change the spi pin, use these spi configuration settings
  Adafruit_USBH_Host USBHost(&SPI, 18, 23, 19, 5, 35);
*/

M5_USBH_Host USBHost(&SPI, 18, 23, 19, 5, 35);
M5Canvas canvas(&M5.Display);

typedef struct {
    tusb_desc_device_t desc_device;
    uint16_t manufacturer[32];
    uint16_t product[48];
    uint16_t serial[16];
    bool mounted;
} dev_info_t;

typedef struct {
    String name;
    uint32_t size;
    bool isDir;
}file_info_t;

dev_info_t dev_info[CFG_TUH_DEVICE_MAX] = {0};

#define MOUSE_BUTTON_LEFT      0x01
#define MOUSE_BUTTON_RIGHT     0x02
#define MOUSE_BUTTON_MIDDLE    0x04
#define MOUSE_BUTTON_SIDE_UP   0x10
#define MOUSE_BUTTON_SIDE_DOWN 0x08

#define MOUSE_MOVE_LEFT  0xFF
#define MOUSE_MOVE_RIGHT 0x01

#define MOUSE_MOVE_UP   0xFF
#define MOUSE_MOVE_DOWN 0x01

int StaPotX = 160, StaPotY = 120;

void Mouse_Pointer(int PotDataX, int PotDataY) {
    static int OldDataX, OldDataY;

    if ((StaPotX + PotDataX) <= 320 && (StaPotX + PotDataX) > 0)
        StaPotX = (StaPotX + PotDataX);
    else if ((StaPotX + PotDataX) <= 0)
        StaPotX = 0;
    else
        StaPotX = 319;

    if ((StaPotY + PotDataY) <= 240 && (StaPotY + PotDataY) > 0)
        StaPotY = (StaPotY + PotDataY);
    else if ((StaPotY + PotDataY) <= 0)
        StaPotY = 0;
    else
        StaPotY = 239;

    // clear draw
    if (OldDataX != StaPotX || OldDataY != StaPotY) {
        M5.Lcd.drawLine(OldDataX + 0, OldDataY + 0, OldDataX + 0, OldDataY + 10,
                        BLACK);
        M5.Lcd.drawLine(OldDataX + 0, OldDataY + 0, OldDataX + 7, OldDataY + 7,
                        BLACK);
        M5.Lcd.drawLine(OldDataX + 4, OldDataY + 7, OldDataX + 7, OldDataY + 7,
                        BLACK);
        M5.Lcd.drawLine(OldDataX + 4, OldDataY + 7, OldDataX + 0, OldDataY + 10,
                        BLACK);
        M5.Lcd.drawLine(OldDataX + 3, OldDataY + 7, OldDataX + 6, OldDataY + 12,
                        BLACK);
    }

    // draw
    M5.Lcd.drawLine(StaPotX + 0, StaPotY + 0, StaPotX + 0, StaPotY + 10, WHITE);
    M5.Lcd.drawLine(StaPotX + 0, StaPotY + 0, StaPotX + 7, StaPotY + 7, WHITE);
    M5.Lcd.drawLine(StaPotX + 4, StaPotY + 7, StaPotX + 7, StaPotY + 7, WHITE);
    M5.Lcd.drawLine(StaPotX + 4, StaPotY + 7, StaPotX + 0, StaPotY + 10, WHITE);
    M5.Lcd.drawLine(StaPotX + 3, StaPotY + 7, StaPotX + 6, StaPotY + 12, WHITE);

    OldDataX = StaPotX;
    OldDataY = StaPotY;
}

extern SemaphoreHandle_t max3421_intr_sem;

void print_lsusb(void) {
    bool no_device = true;
    for (uint8_t daddr = 1; daddr < CFG_TUH_DEVICE_MAX + 1; daddr++) {
        // TODO can use tuh_mounted(daddr), but tinyusb has an bug
        // use local connected flag instead
        dev_info_t *dev = &dev_info[daddr - 1];
        if (dev->mounted) {
            Serial.printf("Device %u: ID %04x:%04x %s %s\r\n", daddr,
                          dev->desc_device.idVendor, dev->desc_device.idProduct,
                          (char *)dev->manufacturer, (char *)dev->product);

            no_device = false;
        }
    }

    if (no_device) {
        Serial.println("No device connected (except hub)");
    }
}

static void _convert_utf16le_to_utf8(const uint16_t *utf16, size_t utf16_len,
                                     uint8_t *utf8, size_t utf8_len) {
    // TODO: Check for runover.
    (void)utf8_len;
    // Get the UTF-16 length out of the data itself.

    for (size_t i = 0; i < utf16_len; i++) {
        uint16_t chr = utf16[i];
        if (chr < 0x80) {
            *utf8++ = chr & 0xff;
        } else if (chr < 0x800) {
            *utf8++ = (uint8_t)(0xC0 | (chr >> 6 & 0x1F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
        } else {
            // TODO: Verify surrogate.
            *utf8++ = (uint8_t)(0xE0 | (chr >> 12 & 0x0F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 6 & 0x3F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
        }
        // TODO: Handle UTF-16 code points that take two entries.
    }
}

// Count how many bytes a utf-16-le encoded string will take in utf-8.
static int _count_utf8_bytes(const uint16_t *buf, size_t len) {
    size_t total_bytes = 0;
    for (size_t i = 0; i < len; i++) {
        uint16_t chr = buf[i];
        if (chr < 0x80) {
            total_bytes += 1;
        } else if (chr < 0x800) {
            total_bytes += 2;
        } else {
            total_bytes += 3;
        }
        // TODO: Handle UTF-16 code points that take two entries.
    }
    return total_bytes;
}

void utf16_to_utf8(uint16_t *temp_buf, size_t buf_len) {
    size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof(uint16_t);
    size_t utf8_len  = _count_utf8_bytes(temp_buf + 1, utf16_len);

    _convert_utf16le_to_utf8(temp_buf + 1, utf16_len, (uint8_t *)temp_buf,
                             buf_len);
    ((uint8_t *)temp_buf)[utf8_len] = '\0';
}

//--------------------------------------------------------------------+
// Print Device Descriptor 
//--------------------------------------------------------------------+

void print_device_descriptor(tuh_xfer_t *xfer) {
    if (XFER_RESULT_SUCCESS != xfer->result) {
        Serial.printf("Failed to get device descriptor\r\n");
        return;
    }

    uint8_t const daddr      = xfer->daddr;
    dev_info_t *dev          = &dev_info[daddr - 1];
    tusb_desc_device_t *desc = &dev->desc_device;

    Serial.printf("Device %u: ID %04x:%04x\r\n", daddr, desc->idVendor,
                  desc->idProduct);
    Serial.printf("Device Descriptor:\r\n");
    Serial.printf("  bLength             %u\r\n", desc->bLength);
    Serial.printf("  bDescriptorType     %u\r\n", desc->bDescriptorType);
    Serial.printf("  bcdUSB              %04x\r\n", desc->bcdUSB);
    Serial.printf("  bDeviceClass        %u\r\n", desc->bDeviceClass);
    Serial.printf("  bDeviceSubClass     %u\r\n", desc->bDeviceSubClass);
    Serial.printf("  bDeviceProtocol     %u\r\n", desc->bDeviceProtocol);
    Serial.printf("  bMaxPacketSize0     %u\r\n", desc->bMaxPacketSize0);
    Serial.printf("  idVendor            0x%04x\r\n", desc->idVendor);
    Serial.printf("  idProduct           0x%04x\r\n", desc->idProduct);
    Serial.printf("  bcdDevice           %04x\r\n", desc->bcdDevice);

    // Get String descriptor using Sync API
    Serial.printf("  iManufacturer       %u     ", desc->iManufacturer);
    if (XFER_RESULT_SUCCESS ==
        tuh_descriptor_get_manufacturer_string_sync(
            daddr, LANGUAGE_ID, dev->manufacturer, sizeof(dev->manufacturer))) {
        utf16_to_utf8(dev->manufacturer, sizeof(dev->manufacturer));
        Serial.printf((char *)dev->manufacturer);
    }
    Serial.printf("\r\n");

    Serial.printf("  iProduct            %u     ", desc->iProduct);
    if (XFER_RESULT_SUCCESS ==
        tuh_descriptor_get_product_string_sync(daddr, LANGUAGE_ID, dev->product,
                                               sizeof(dev->product))) {
        utf16_to_utf8(dev->product, sizeof(dev->product));
        Serial.printf((char *)dev->product);
    }
    Serial.printf("\r\n");

    Serial.printf("  iSerialNumber       %u     ", desc->iSerialNumber);
    if (XFER_RESULT_SUCCESS ==
        tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID, dev->serial,
                                              sizeof(dev->serial))) {
        utf16_to_utf8(dev->serial, sizeof(dev->serial));
        Serial.printf((char *)dev->serial);
    }
    Serial.printf("\r\n");

    Serial.printf("  bNumConfigurations  %u\r\n", desc->bNumConfigurations);

    // print device summary
    print_lsusb();
}

void report_parse(uint8_t const* report) {
    if (xSemaphoreTake(max3421_intr_sem, 0)) {
        Serial.printf("%d %d\r\n", (int8_t)report[1], (int8_t)report[2]);
        Mouse_Pointer((int8_t)report[1], (int8_t)report[2]);
        if (report[0] == MOUSE_BUTTON_LEFT)  // left button pressed
            M5.Lcd.drawCircle(StaPotX, StaPotY, 1, WHITE);
        if (report[0] == MOUSE_BUTTON_RIGHT)  // right button pressed
            M5.Lcd.drawCircle(StaPotX, StaPotY, 1, GREEN);
        if (report[0] == MOUSE_BUTTON_MIDDLE)  // middle button pressed
            M5.Lcd.fillScreen(BLACK);
        xSemaphoreGive(max3421_intr_sem);
    }
    // Serial.printf("%d %d\r\n", (int8_t)report[1], (int8_t)report[2]);
}

static uint8_t report_data[4] = {0};

void setup() {
    M5.begin();
    M5.Power.begin();
    Serial.println("M5Module USB HOST HID");

    // Init USB Host on native controller roothub port0
    if (USBHost.begin(0)) {
        Serial.println("MODULE INIT OK");
    } else {
        Serial.println("MODULE INIT ERROR");
    }
}

void loop() {
    USBHost.task();
    // report_parse(report_data);
    // USBHost2.task();
}

void tuh_hid_report_sent_cb(uint8_t dev_addr, uint8_t idx,
                            uint8_t const* report, uint16_t len) {
}

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use.
// tuh_hid_parse_report_descriptor() can be used to parse common/simple enough
// descriptor. Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE,
// it will be skipped therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    Serial.printf("HID device address = %d, instance = %d is mounted\r\n",
                  dev_addr, instance);
    Serial.printf("VID = %04x, PID = %04x\r\n", vid, pid);
    if (!tuh_hid_receive_report(dev_addr, instance)) {
        Serial.printf("Error: cannot request to receive report\r\n");
    }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    Serial.printf("HID device address = %d, instance = %d is unmounted\r\n",
                  dev_addr, instance);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    Serial.printf("HIDreport : ");
    for (uint16_t i = 0; i < len; i++) {
        Serial.printf("0x%02X ", report[i]);
    }
    Serial.println();
    // if (len == 4) {
    //     // report_parse(report);
    //     // report_parse(report);
        // memcpy(report_data, report, len);
        report_parse(report);
    // }
    // continue to request to receive report
    if (!tuh_hid_receive_report(dev_addr, instance)) {
        Serial.printf("Error: cannot request to receive report\r\n");
    }
}

//--------------------------------------------------------------------+
// TinyUSB Host callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted (configured)
void tuh_mount_cb(uint8_t daddr) {
    Serial.printf("Device attached, address = %d\r\n", daddr);

    dev_info_t* dev = &dev_info[daddr - 1];
    dev->mounted    = true;
    // Get Device Descriptor
    tuh_descriptor_get_device(daddr, &dev->desc_device, 18,
                              print_device_descriptor, 0);
}

/// Invoked when device is unmounted (bus reset/unplugged)
void tuh_umount_cb(uint8_t daddr) {
    Serial.printf("Device removed, address = %d\r\n", daddr);
    dev_info_t* dev = &dev_info[daddr - 1];
    dev->mounted    = false;

    // print device summary
    print_lsusb();
}

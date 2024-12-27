
/*
 * SPDX-FileCopyrightText: 2024 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @Dependent Library:
 * M5GFX: https://github.com/m5stack/M5GFX
 * M5Unified: https://github.com/m5stack/M5Unified
 * M5-Max3421E-USBShield: https://github.com/m5stack/M5-Max3421E-USBShield
 */
#include "M5Unified.h"
#include "M5_Max3421E_Usb.h"

#define rIOPINS1 (0xa0)  // 20<<3
/* IOPINS1 Bits */
#define bmGPOUT0 (0x01)
#define bmGPOUT1 (0x02)
#define bmGPOUT2 (0x04)
#define bmGPOUT3 (0x08)
#define bmGPIN0  (0x10)
#define bmGPIN1  (0x20)
#define bmGPIN2  (0x40)
#define bmGPIN3  (0x80)

#define rIOPINS2 (0xa8)  // 21<<3
/* IOPINS2 Bits */
#define bmGPOUT4 (0x01)
#define bmGPOUT5 (0x02)
#define bmGPOUT6 (0x04)
#define bmGPOUT7 (0x08)
#define bmGPIN4  (0x10)
#define bmGPIN5  (0x20)
#define bmGPIN6  (0x40)
#define bmGPIN7  (0x80)

/*
  The board is equipped with two dip switches to adapt to different M5 series hosts.
  https://static-cdn.m5stack.com/resource/docs/products/module/USB%20v1.2%20Module/pinMap-70b8e2ad-8325-4887-af33-44e3dae91520.png
  If you need to change the spi pin, use these spi configuration settings
  M5_USBH_Host USBHost(&SPI, 18, 23, 19, 5, 35);
*/
// Initialize USB Host
M5_USBH_Host USBHost(&SPI, 18, 23, 19, 5, 35);

/**
 * @brief Sets the output pin level.
 *
 * pin range: 0~4
 * value sets the state: 0 for low level, 1 for high level.
 *
 * @param pin The pin number to set (0~4).
 * @param value The desired state of the pin (0 for low level, 1 for high level).
 */
void write_output_pin(uint8_t pin, uint8_t value);

/**
 * @brief Reads the output pin level.
 *
 * pin range: 0~4
 *
 * @param pin The pin number to read (0~4).
 *
 * @return Returns 1 for high level, 0 for low level.
 */
uint8_t read_output_pin(uint8_t pin);

/**
 * @brief Prints the level of the specified pin.
 *
 * Prints the state of the given pin (high or low).
 *
 * @param pin The pin number to print (0~4).
 */
void print_pin_level(uint8_t pin);

void setup()
{
    M5.begin();
    M5.Power.begin();
    Serial.begin(115200);

    Serial.println("Initializing MAX3421E with USB Host...");

    // Initialize USB Host
    if (USBHost.begin(0)) {
        Serial.println("USB Host initialized successfully.");
    } else {
        Serial.println("Failed to initialize USB Host.");
        while (1) {
        };
    }
    write_output_pin(0, 0);
    write_output_pin(1, 1);
    write_output_pin(2, 0);
    write_output_pin(3, 1);
    write_output_pin(4, 0);
}

void loop()
{
    for (uint8_t i = 0; i <= 4; i++) {
        print_pin_level(i);
        delay(100);
    }
    delay(3000);
}

void write_output_pin(uint8_t pin, uint8_t value)
{
    uint8_t gpout = USBHost.max3421_readRegister(rIOPINS1, false);
    gpout &= 0x0f;
    gpout |= (USBHost.max3421_readRegister(rIOPINS2, false) << 4);
    if (value) {
        gpout |= (0x01 << pin);
    } else {
        gpout &= ~(0x01 << pin);
    }
    USBHost.max3421_writeRegister(rIOPINS1, (gpout & 0x0F), false);
    USBHost.max3421_writeRegister(rIOPINS2, (gpout >> 4), false);
}

uint8_t read_output_pin(uint8_t pin)
{
    uint8_t gpout = USBHost.max3421_readRegister(rIOPINS1, false);
    gpout &= 0x0f;
    gpout |= (USBHost.max3421_readRegister(rIOPINS2, false) << 4);
    return ((gpout >> pin) & 0x01);
}

void print_pin_level(uint8_t pin)
{
    if (read_output_pin(pin)) {
        Serial.printf("GPOUT%d is high\r\n", pin);
    } else {
        Serial.printf("GPOUT%d is low\r\n", pin);
    }
}

void tuh_hid_report_sent_cb(uint8_t dev_addr, uint8_t idx, uint8_t const* report, uint16_t len)
{
}
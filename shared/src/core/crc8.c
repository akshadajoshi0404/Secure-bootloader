#include "core/crc8.h"
#include "common-defines.h"


volatile int x = 0;
/***********************
 * CRC-8 Calculation
 * Polynomial: x^8 + x^2 + x + 1 (0x07)
 * Initial value: 0x00
 ***********************/
uint8_t crc8_compute(const uint8_t* data, uint32_t length)
{
    uint8_t crc = 0x00; // Initial value

    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= data[i]; // XOR byte into CRC

        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x80) // If the MSB is set
            {
                crc = (crc << 1) ^ 0x07; // Shift left and XOR with polynomial
            }
            else
            {
                crc <<= 1; // Just shift left
            }
        }
        x++;
    }

    return crc;
}


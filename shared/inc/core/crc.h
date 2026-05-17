#ifndef CRC_H_
#define CRC_H_

#include "common-defines.h"

uint8_t crc8_compute(const uint8_t* data, uint32_t length);
uint32_t crc32(const uint8_t* data, const uint32_t length);

#endif /* CRC_H_ */
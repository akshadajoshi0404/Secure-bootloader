#ifndef INC_COMMS_H_
#define INC_COMMS_H_

#include "common-defines.h"

#define PACKET_DATA_LENGTH  (16)
#define PACKET_LENGTH_BYTE  (1)
#define PACKET_CRC_BYTE     (1)
#define PACKET_TOTAL_LENGTH (PACKET_LENGTH_BYTE + PACKET_DATA_LENGTH + PACKET_CRC_BYTE)


#define PACKET_RETX_DATA0 (0x19)
#define PACKET_ACK_DATA0  (0x15)

typedef struct comms_packet
{
    uint8_t length;
    uint8_t data[PACKET_DATA_LENGTH];
    uint8_t crc;
} comms_packet_t;

void comms_setup(void);
void comms_update(void);

bool comms_packet_available(void);
void comms_send_packet(const comms_packet_t* packet);
void comms_receive_packet(comms_packet_t* packet);
uint8_t comms_calculate_crc(const comms_packet_t* packet);
bool comms_is_single_byte_packet(const comms_packet_t* packet, uint8_t byte);

#endif /* INC_COMMS_H_ */
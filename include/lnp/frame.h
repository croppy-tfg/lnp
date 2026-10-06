/**
 * @file    frame.h
 * @author  Jose
 * @date    5/10/26
 * @project lnp
 */
#ifndef FRAME_H
#define FRAME_H

#include "lnp/types.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * DUTY CYCLE WARNING (EU868)
 * - Beacon (15B on-air) = ~1.4s TX
 * - Full frame (255B on-air) = ~8.2s TX
 * Being the 1% duty cycle mandatory, a 255B packet
 * consumes ~13.5m. We need to increase the timer.
 */

#define LNP_VERSION      1

// Wire header = 12B  ->  255B (max LoRa) - 12B = 243B payload
#define LNP_HEADER_SIZE  12
#define LNP_MAX_PAYLOAD  243

_Static_assert(LNP_HEADER_SIZE + LNP_MAX_PAYLOAD == 255, "frame must fit one LoRa packet");

typedef enum {
    LNP_FRAME_DATA   = 0,
    LNP_FRAME_BEACON = 1
} lnp_frame_type_t;

typedef struct {
    uint8_t ver_type; // version (hi nibble) | frame type (lo nibble)
    uint8_t len;      // Payload length
    uint8_t ttl;      // Frame Time-To-Live
    uint8_t seq;      // Sequence number (prevents dupes)
    lna_t src;        // Source node
    lna_t dst;        // Destination node
    lna_t prev_hop;   // Link-layer transmitter
    lna_t next_hop;   // Link-layer receiver
} lnp_header_t;

typedef struct {
    lnp_header_t header;
    uint8_t payload[LNP_MAX_PAYLOAD];
} lnp_frame_t;

static inline lnp_frame_type_t lnp_frame_type(const lnp_frame_t *f)
{
    return (lnp_frame_type_t)(f->header.ver_type & 0x0Fu);
}

void lnp_frame_init(lnp_frame_t *frame, lnp_frame_type_t type, lna_t src, lna_t dst, uint8_t ttl, uint8_t seq);
bool lnp_frame_set_payload(lnp_frame_t *frame, const uint8_t *data, uint8_t len);
uint16_t lnp_frame_serialize(const lnp_frame_t *frame, uint8_t *buffer, uint16_t buffer_size);
bool lnp_frame_deserialize(lnp_frame_t *frame, const uint8_t *buffer, uint16_t buffer_size);

#endif //FRAME_H

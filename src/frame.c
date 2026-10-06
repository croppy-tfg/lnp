/**
 * @file    frame.c
 * @author  Jose
 * @date    5/10/26
 * @project lnp
 */

#include "lnp/frame.h"
#include <string.h>

// manual aligning
static void put_u16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)(v >> 8);
}

static uint16_t get_u16(const uint8_t *b)
{
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

void lnp_frame_init(lnp_frame_t *frame, lnp_frame_type_t type, lna_t src, lna_t dst, uint8_t ttl, uint8_t seq)
{
    if (!frame) return;

    frame->header.ver_type = (uint8_t)((LNP_VERSION << 4) | ((unsigned)type & 0x0Fu));
    frame->header.src = src;
    frame->header.dst = dst;
    frame->header.prev_hop = src;
    frame->header.next_hop = dst;
    frame->header.ttl = ttl;
    frame->header.seq = seq;
    frame->header.len = 0;
    memset(frame->payload, 0, LNP_MAX_PAYLOAD);
}

bool lnp_frame_set_payload(lnp_frame_t *frame, const uint8_t *data, uint8_t len)
{
    if (!frame || len > LNP_MAX_PAYLOAD) return false;
    if (len > 0 && !data) return false;

    if (len > 0) memcpy(frame->payload, data, len);
    frame->header.len = len;
    return true;
}

uint16_t lnp_frame_serialize(const lnp_frame_t *frame, uint8_t *buffer, uint16_t buffer_size)
{
    // len > MAX_PAYLOAD would read past payload[] -> reject
    if (!frame || !buffer || frame->header.len > LNP_MAX_PAYLOAD) return 0;

    uint16_t total_len = (uint16_t)(LNP_HEADER_SIZE + frame->header.len);
    if (buffer_size < total_len) return 0;

    const lnp_header_t *h = &frame->header;
    buffer[0] = h->ver_type;
    buffer[1] = h->len;
    buffer[2] = h->ttl;
    buffer[3] = h->seq;
    put_u16(buffer + 4, h->src.raw);
    put_u16(buffer + 6, h->dst.raw);
    put_u16(buffer + 8, h->prev_hop.raw);
    put_u16(buffer + 10, h->next_hop.raw);

    if (h->len > 0)
        memcpy(buffer + LNP_HEADER_SIZE, frame->payload, h->len);

    return total_len;
}

bool lnp_frame_deserialize(lnp_frame_t *frame, const uint8_t *buffer, uint16_t buffer_size)
{
    // valid ptrs and minimum frame length (header only) check
    if (!frame || !buffer || buffer_size < LNP_HEADER_SIZE) return false;
    if ((buffer[0] >> 4) != LNP_VERSION) return false;

    uint8_t payload_len = buffer[1];
    if (payload_len > LNP_MAX_PAYLOAD) return false;

    if (buffer_size < (uint16_t)(LNP_HEADER_SIZE + payload_len)) return false;

    lnp_header_t *h = &frame->header;
    h->ver_type = buffer[0];
    h->len      = buffer[1];
    h->ttl      = buffer[2];
    h->seq      = buffer[3];
    h->src.raw      = get_u16(buffer + 4);
    h->dst.raw      = get_u16(buffer + 6);
    h->prev_hop.raw = get_u16(buffer + 8);
    h->next_hop.raw = get_u16(buffer + 10);

    memset(frame->payload, 0, LNP_MAX_PAYLOAD);
    if (payload_len > 0)
        memcpy(frame->payload, buffer + LNP_HEADER_SIZE, payload_len);

    return true;
}

/**
 * @file    types.h
 * @author  Jose
 * @date    5/10/26
 * @project lnp
 */
#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define LNP_ZONE_GLOBAL 0x3FF
#define LNP_NODE_BROADCAST 0xF

typedef enum {
    LNP_TYPE_GATEWAY  = 0, // 'g'
    LNP_TYPE_ROOT     = 1, // 'r'
    LNP_TYPE_LEAF     = 2, // 'l'
    LNP_TYPE_RESERVED = 3  // '#'
} lnp_node_type_t;

typedef struct {
    uint16_t raw;
} lna_t;

_Static_assert(sizeof(lna_t) == 2, "lna_t must be exactly 2 bytes");

static inline lna_t lna_make(lnp_node_type_t type, uint16_t zone, uint8_t node)
{
    lna_t a;
    a.raw = (uint16_t)((((uint16_t)type & 0x3u) << 14) |
                       ((zone & 0x3FFu) << 4) |
                       (node & 0xFu));
    return a;
}

static inline lnp_node_type_t lna_type(lna_t a) { return (lnp_node_type_t)((a.raw >> 14) & 0x3u); }
static inline uint16_t        lna_zone(lna_t a) { return (uint16_t)((a.raw >> 4) & 0x3FFu); }
static inline uint8_t         lna_node(lna_t a) { return (uint8_t)(a.raw & 0xFu); }
static inline bool            lna_eq(lna_t a, lna_t b) { return a.raw == b.raw; }

static inline lna_t lna_zone_broadcast(uint16_t zone)
{
    return lna_make(LNP_TYPE_RESERVED, zone, LNP_NODE_BROADCAST);
}

static inline lna_t lna_broadcast(void)
{
    return lna_make(LNP_TYPE_RESERVED, LNP_ZONE_GLOBAL, LNP_NODE_BROADCAST);
}

static inline bool lna_is_broadcast(lna_t a)
{
    return lna_type(a) == LNP_TYPE_RESERVED && lna_node(a) == LNP_NODE_BROADCAST;
}

static inline bool lna_is_broadcast_for_me(lna_t a, lna_t my_addr)
{
    if (!lna_is_broadcast(a)) return false;
    uint16_t z = lna_zone(a);
    return (z == LNP_ZONE_GLOBAL) || (z == lna_zone(my_addr));
}

void lna_to_str(lna_t addr, char *out_str, size_t max_len);

/* Strict parser ("l.2A3.F"). Returns 0 ok / -1 error; out_addr untouched on error. */
int lna_from_str(const char *in_str, lna_t *out_addr);

#endif //TYPES_H

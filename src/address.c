/**
 * @file    address.c
 * @author  Jose
 * @date    5/10/26
 * @project LNP
 */

#include <stdio.h>

#include "lnp/types.h"

// in-house parser for not depending on standard string libs (like sscanf)
static bool parse_hex(const char **p, unsigned max_digits, unsigned *out)
{
    unsigned value = 0, digits = 0;

    while (digits < max_digits) {
        char c = **p;
        unsigned d;
        if      (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A') + 10u;
        else break;
        value = (value << 4) | d;
        (*p)++;
        digits++;
    }

    if (digits == 0) return false;
    *out = value;
    return true;
}

void lna_to_str(lna_t addr, char *out_str, size_t max_len)
{
    static const char type_chars[4] = { 'g', 'r', 'l', '#' };

    if (!out_str || max_len == 0) return;

    snprintf(out_str, max_len, "%c.%03X.%X",
             type_chars[lna_type(addr)],
             (unsigned)lna_zone(addr),
             (unsigned)lna_node(addr));
}

int lna_from_str(const char *in_str, lna_t *out_addr)
{
    if (!in_str || !out_addr) return -1;

    lnp_node_type_t type;
    switch (in_str[0]) {
    case 'g': type = LNP_TYPE_GATEWAY;  break;
    case 'r': type = LNP_TYPE_ROOT;     break;
    case 'l': type = LNP_TYPE_LEAF;     break;
    case '#': type = LNP_TYPE_RESERVED; break;
    default:  return -1;
    }

    const char *p = in_str + 1;
    unsigned zone, node;

    if (*p++ != '.') return -1;
    if (!parse_hex(&p, 3, &zone) || zone > LNP_ZONE_GLOBAL) return -1;
    if (*p++ != '.') return -1;
    if (!parse_hex(&p, 1, &node)) return -1;
    if (*p != '\0') return -1;               // no trailing NUL char garbage

    *out_addr = lna_make(type, (uint16_t)zone, (uint8_t)node);
    return 0;
}

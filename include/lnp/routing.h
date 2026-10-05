/**
 * @file    routing.h
 * @author  Jose
 * @date    5/10/26
 * @project lnp
 */
#ifndef ROUTING_H
#define ROUTING_H

#include "lnp/frame.h"
#include <stdint.h>
#include <stdbool.h>

#define LNP_COST_INFINITY      0xFF
#define LNP_BEACON_TIMEOUT_MS  60000UL
#define LNP_SEQ_RESYNC_MS      (5UL * LNP_BEACON_TIMEOUT_MS) // no route this long -> forget gateway seq (gateway reboot)
#define LNP_ROUTE_TIMEOUT_MS   600000UL                      // learned downward routes expire after 10 min
#define LNP_DEFAULT_TTL        8
#define LNP_MAX_ROUTES         16
#define LNP_DEDUP_SIZE         16

typedef struct {
    lna_t    dst;
    lna_t    next_hop;
    uint32_t last_seen_ms;
    bool     valid;
} lnp_route_entry_t;

typedef struct {
    lna_t   src;
    uint8_t seq;
    bool    valid;
} lnp_dedup_entry_t;

typedef struct {
    lna_t    self_addr;
    bool     is_gateway;

    // upward route
    lna_t    default_gateway;      // Next hop to gateway (parent)
    uint8_t  current_cost;         // Distance to gateway (in hops)
    uint16_t beacon_seq;           // Freshest gateway-originated beacon seq seen
    bool     have_seq;             // false until the first beacon is heard
    uint32_t last_beacon_ms;       // Timestamp for route timeout
    bool     has_route_to_gateway;
    bool     poison_pending;       // Send one cost=INFINITY beacon so children drop us fast

    // downward routes
    lnp_route_entry_t routes[LNP_MAX_ROUTES];

    // duplicate suppression
    lnp_dedup_entry_t dedup[LNP_DEDUP_SIZE];
    uint8_t           dedup_head;

    uint8_t  tx_seq;               // Seq for frames originated here
} lnp_router_t;

typedef enum {
    LNP_ACTION_CONSUME, // Local processing
    LNP_ACTION_FORWARD, // Forward to next hop (frame already rewritten, just transmit it)
    LNP_ACTION_DROP,    // Drop frame
    LNP_ACTION_HANDLED  // Control frame (beacon) fully processed internally
} lnp_action_t;

/* Gateway = node whose address type is LNP_TYPE_GATEWAY (cost 0, originates beacon seq). */
void lnp_router_init(lnp_router_t *router, lna_t self_addr);

/* Gateway only: seed the beacon seq (random / persisted) so a reboot doesn't look "old". */
void lnp_router_seed_gateway_seq(lnp_router_t *router, uint16_t seed);

/* Call periodically (e.g. every second): expires the route after LNP_BEACON_TIMEOUT_MS. */
void lnp_router_tick(lnp_router_t *router, uint32_t now_ms);

void lnp_router_handle_beacon(lnp_router_t *router, lna_t sender, uint8_t cost, uint16_t seq, uint32_t now_ms);

/* Feed every received (and deserialized) frame here. */
lnp_action_t lnp_router_route(lnp_router_t *router, lnp_frame_t *frame, uint32_t now_ms);

/* Build a frame originated by this node. Returns false if there is no route / payload too big. */
bool lnp_router_originate(lnp_router_t *router, lnp_frame_t *frame, lna_t dst,
                          const uint8_t *data, uint8_t len, uint32_t now_ms);

/* Gateway: always (bumps seq). Others: only with a route (or once, as poison, after losing it). */
bool lnp_router_build_beacon(lnp_router_t *router, lnp_frame_t *frame);

#endif //ROUTING_H

/**
 * @file    routing.c
 * @author  Jose
 * @date    5/10/26
 * @project lnp
 */

#include "lnp/routing.h"
#include <string.h>

#define LNP_BEACON_LEN 5 // cost (1B) + seq (2B) + gw_id (2B)

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

// 16-bit serial-number arithmetic: a is "newer" than b if it is ahead by < 32768
static bool seq_after(uint16_t a, uint16_t b)
{
    uint16_t d = (uint16_t)(a - b);
    return d != 0 && d < 0x8000u;
}

// wrap-safe for the 32-bit ms counter (~49 days)
static bool elapsed_ge(uint32_t now_ms, uint32_t since_ms, uint32_t span_ms)
{
    return (uint32_t)(now_ms - since_ms) >= span_ms;
}

static void invalidate_route(lnp_router_t *r)
{
    r->has_route_to_gateway = false;
    r->current_cost = LNP_COST_INFINITY;
    r->poison_count = 3;
}

static void accept_route(lnp_router_t *r, lna_t parent, uint8_t cost, uint16_t seq, lna_t gw_id, uint32_t now_ms)
{
    r->default_gateway = parent;
    r->gateway_id = gw_id;
    r->current_cost = cost;
    r->beacon_seq = seq;
    r->have_seq = true;
    r->has_route_to_gateway = true;
    r->poison_count = 0;
    r->last_beacon_ms = now_ms;
}

/* ------------------------------------------------------------------ */
/* downward routes + dedup                                             */
/* ------------------------------------------------------------------ */

static lnp_route_entry_t *route_lookup(lnp_router_t *r, lna_t dst, uint32_t now_ms)
{
    for (int i = 0; i < LNP_MAX_ROUTES; i++) {
        lnp_route_entry_t *e = &r->routes[i];
        if (e->valid && lna_eq(e->dst, dst)) {
            if (elapsed_ge(now_ms, e->last_seen_ms, LNP_ROUTE_TIMEOUT_MS)) {
                e->valid = false;
                return NULL;
            }
            return e;
        }
    }
    return NULL;
}

// "I got a frame from `dst` through neighbour `via`  =>  `via` is my next hop towards `dst`"
static void route_learn(lnp_router_t *r, lna_t dst, lna_t via, uint32_t now_ms)
{
    if (lna_eq(dst, r->self_addr) || lna_is_broadcast(dst) || lna_eq(via, r->self_addr))
        return;

    lnp_route_entry_t *existing = NULL, *free_slot = NULL, *oldest = &r->routes[0];
    uint32_t oldest_age = 0;

    for (int i = 0; i < LNP_MAX_ROUTES; i++) {
        lnp_route_entry_t *e = &r->routes[i];
        if (!e->valid) {
            if (!free_slot) free_slot = e;
            continue;
        }
        if (lna_eq(e->dst, dst)) { existing = e; break; }
        uint32_t age = (uint32_t)(now_ms - e->last_seen_ms);
        if (age >= oldest_age) { oldest_age = age; oldest = e; }
    }

    lnp_route_entry_t *slot = existing ? existing : (free_slot ? free_slot : oldest);

    slot->dst = dst;
    slot->next_hop = via;
    slot->last_seen_ms = now_ms;
    slot->valid = true;
}

// returns true if (src,seq) was already seen; otherwise remembers it
static bool dedup_seen(lnp_router_t *r, lna_t src, uint8_t seq)
{
    for (int i = 0; i < LNP_DEDUP_SIZE; i++) {
        const lnp_dedup_entry_t *e = &r->dedup[i];
        if (e->valid && e->seq == seq && lna_eq(e->src, src)) return true;
    }
    r->dedup[r->dedup_head].src = src;
    r->dedup[r->dedup_head].seq = seq;
    r->dedup[r->dedup_head].valid = true;
    r->dedup_head = (uint8_t)((r->dedup_head + 1u) % LNP_DEDUP_SIZE);
    return false;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

void lnp_router_init(lnp_router_t *router, lna_t self_addr)
{
    if (!router) return;

    memset(router, 0, sizeof(*router));
    router->self_addr = self_addr;
    router->is_gateway = (lna_type(self_addr) == LNP_TYPE_GATEWAY);

    if (router->is_gateway) {
        // the gateway is the root of the tree: it always has a "route" (to itself)
        router->current_cost = 0;
        router->has_route_to_gateway = true;
        router->have_seq = true;
        router->default_gateway = self_addr;
    } else {
        router->current_cost = LNP_COST_INFINITY;
    }
}

void lnp_router_seed_gateway_seq(lnp_router_t *router, uint16_t seed)
{
    if (!router || !router->is_gateway) return;
    router->beacon_seq = seed;
}

void lnp_router_tick(lnp_router_t *router, uint32_t now_ms)
{
    if (!router || router->is_gateway) return;

    if (router->has_route_to_gateway) {
        if (elapsed_ge(now_ms, router->last_beacon_ms, LNP_BEACON_TIMEOUT_MS))
            invalidate_route(router);
    } else if (router->have_seq) {
        // Long silence: maybe the gateway rebooted and its seq restarted. Forget ours.
        if (elapsed_ge(now_ms, router->last_beacon_ms, LNP_SEQ_RESYNC_MS))
            router->have_seq = false;
    }
}

void lnp_router_handle_beacon(lnp_router_t *router, lna_t sender, uint8_t cost, uint16_t seq, lna_t gw_id, uint32_t now_ms)
{
    if (!router || router->is_gateway) return;
    if (lna_eq(sender, router->self_addr)) return;

    if (lna_type(router->self_addr) == LNP_TYPE_LEAF && lna_type(sender) == LNP_TYPE_LEAF) return;

    bool from_parent = router->has_route_to_gateway && lna_eq(sender, router->default_gateway);

    // the cost the neighbour advertises is ITS distance; ours would be +1
    uint8_t offered = (cost >= LNP_COST_INFINITY) ? LNP_COST_INFINITY : (uint8_t)(cost + 1u);

    if (offered >= LNP_COST_INFINITY) {
        // poison / unusable: only matters if it comes from our parent
        if (from_parent) invalidate_route(router);
        return;
    }

    bool newer = !router->have_seq || seq_after(seq, router->beacon_seq);
    bool same  = router->have_seq && seq == router->beacon_seq;

    if (from_parent) {
        // follow the parent even if its cost got worse, but only on fresh info
        if (newer) accept_route(router, sender, offered, seq, gw_id, now_ms);
        return;
    }

    if (!router->has_route_to_gateway) {
        // Only a beacon NEWER than the route we lost is trustworthy. Older/equal ones may
        // come from our own children still advertising a path through us -> routing loop.
        if (newer) accept_route(router, sender, offered, seq, gw_id, now_ms);
        return;
    }

    // Alive route and a different neighbour: switch only if strictly better
    if ((newer || same) && offered < router->current_cost)
        accept_route(router, sender, offered, seq, gw_id, now_ms);
}

static void process_beacon(lnp_router_t *r, const lnp_frame_t *f, uint32_t now_ms)
{
    if (f->header.len < LNP_BEACON_LEN) return;
    if (!lna_is_broadcast(f->header.dst)) return;

    uint8_t  cost = f->payload[0];
    uint16_t seq  = (uint16_t)(f->payload[1] | ((uint16_t)f->payload[2] << 8));
    lna_t gw_id;
    gw_id.raw = (uint16_t)(f->payload[3]) | ((uint16_t)f->payload[4] << 8);

    lnp_router_handle_beacon(r, f->header.src, cost, seq, gw_id, now_ms);
}

// Decide next_hop for frame->header.dst. Order: learned route > same zone (1 hop) > default gateway
static bool select_next_hop(lnp_router_t *r, lnp_frame_t *frame, uint32_t now_ms)
{
    lna_t dst = frame->header.dst;

    const lnp_route_entry_t *e = route_lookup(r, dst, now_ms);
    if (e) {
        frame->header.next_hop = e->next_hop;
        return true;
    }

    // intra-zone p2p (assumes every node of a zone is in radio range of the others)
    if (lna_zone(dst) == lna_zone(r->self_addr)) {
        frame->header.next_hop = dst;
        return true;
    }

    // inter-zone: up the tree. The gateway has nowhere "up" to send it.
    if (!r->is_gateway && r->has_route_to_gateway) {
        frame->header.next_hop = r->default_gateway;
        return true;
    }

    return false;
}

lnp_action_t lnp_router_route(lnp_router_t *router, lnp_frame_t *frame, uint32_t now_ms)
{
    if (!router || !frame) return LNP_ACTION_DROP;
    lnp_header_t *h = &frame->header;

    // beacons are link-local control traffic: never forwarded
    if (lnp_frame_type(frame) == LNP_FRAME_BEACON) {
        process_beacon(router, frame, now_ms);
        return LNP_ACTION_HANDLED;
    }

    // LoRa is a shared medium: we hear frames meant for other nodes. Not for us -> ignore.
    if (!lna_eq(h->next_hop, router->self_addr) && !lna_is_broadcast_for_me(h->next_hop, router->self_addr))
        return LNP_ACTION_DROP;

    // our own frame coming back = loop
    if (lna_eq(h->src, router->self_addr)) return LNP_ACTION_DROP;

    // duplicates
    if (dedup_seen(router, h->src, h->seq)) return LNP_ACTION_DROP;

    // learn the reverse path (src is reachable through whoever just transmitted this)
    route_learn(router, h->src, h->prev_hop, now_ms);

    // is it for me? (broadcast data is consumed locally, never flooded)
    if (lna_eq(h->dst, router->self_addr) || lna_is_broadcast_for_me(h->dst, router->self_addr))
        return LNP_ACTION_CONSUME;

    if (lna_type(router->self_addr) == LNP_TYPE_LEAF) return LNP_ACTION_DROP;

    // prevent loops via ttl
    if (h->ttl <= 1) return LNP_ACTION_DROP;
    h->ttl--;

    lna_t came_from = h->prev_hop;
    if (!select_next_hop(router, frame, now_ms)) return LNP_ACTION_DROP;

    // never bounce a frame straight back to the node that just gave it to us
    if (lna_eq(h->next_hop, came_from)) return LNP_ACTION_DROP;

    h->prev_hop = router->self_addr;
    return LNP_ACTION_FORWARD;
}

bool lnp_router_originate(lnp_router_t *router, lnp_frame_t *frame, lna_t dst,
                          const uint8_t *data, uint8_t len, uint32_t now_ms)
{
    if (!router || !frame) return false;

    lnp_frame_init(frame, LNP_FRAME_DATA, router->self_addr, dst, LNP_DEFAULT_TTL, router->tx_seq);
    if (!lnp_frame_set_payload(frame, data, len)) return false;
    if (!select_next_hop(router, frame, now_ms)) return false;

    router->tx_seq++;
    return true;
}

bool lnp_router_build_beacon(lnp_router_t *router, lnp_frame_t *frame)
{
    if (!router || !frame) return false;

    if (lna_type(router->self_addr) == LNP_TYPE_LEAF) return false;

    uint8_t  cost;
    uint16_t seq;

    if (router->is_gateway) {
        // only the gateway originates a new seq; everyone else just relays it
        router->beacon_seq++;
        cost = 0;
        seq = router->beacon_seq;
    } else if (router->has_route_to_gateway) {
        cost = router->current_cost;
        seq = router->beacon_seq;
    } else if (router->poison_count > 0) {
        cost = LNP_COST_INFINITY;           // "I lost my route, don't use me"
        seq = router->beacon_seq;
        router->poison_count--;
    } else {
        return false;
    }

    lnp_frame_init(frame, LNP_FRAME_BEACON, router->self_addr, lna_broadcast(), 1, (uint8_t)(seq & 0xFFu));

    uint8_t body[LNP_BEACON_LEN] = {
        cost,
        (uint8_t)(seq & 0xFFu), (uint8_t)(seq >> 8),
        (uint8_t)(router->gateway_id.raw & 0xFFu), (uint8_t)(router->gateway_id.raw >> 8)
    };
    return lnp_frame_set_payload(frame, body, sizeof(body));
}

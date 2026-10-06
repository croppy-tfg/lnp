/**
 * @file    main.c
 * @author  Jose
 * @date    5/10/26
 * @project lnp
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "lnp/types.h"
#include "lnp/frame.h"
#include "lnp/routing.h"

/* Helper to trace routing steps clearly */
static void print_trace(const char *step, lna_t src, lna_t prev, lna_t next, lna_t dst, uint8_t ttl)
{
    char s_src[16], s_prev[16], s_next[16], s_dst[16];
    lna_to_str(src, s_src, sizeof(s_src));
    lna_to_str(prev, s_prev, sizeof(s_prev));
    lna_to_str(next, s_next, sizeof(s_next));
    lna_to_str(dst, s_dst, sizeof(s_dst));

    printf("    [%s] Origen: %s -> Destino: %s | Salto: %s -> %s | TTL: %u\n",
           step, s_src, s_dst, s_prev, s_next, ttl);
}

int main(void)
{
    printf("====================================================\n");
    printf("        INICIANDO SIMULADOR DE PROTOCOLO LNPv1      \n");
    printf("====================================================\n\n");

    uint32_t current_time_ms = 1000;

    /* ------------------------------------------------------------
     * TEST 1: Strict addressing and formatting
     * ------------------------------------------------------------ */
    printf("[TEST 1] Verificando parseo estricto de direcciones...\n");
    lna_t parsed_addr;

    // Valid assignable node
    printf("  > Parseando 'r.007.1' (Nodo legal)...\n");
    assert(lna_from_str("r.007.1", &parsed_addr) == 0);
    assert(lna_type(parsed_addr) == LNP_TYPE_ROOT);
    assert(lna_zone(parsed_addr) == 7);
    assert(lna_node(parsed_addr) == 1);

    char str_buf[16];
    lna_to_str(parsed_addr, str_buf, sizeof(str_buf));
    assert(strcmp(str_buf, "r.007.1") == 0);
    printf("  > OK: Parseo y formateo correctos.\n");

    // Invalid node (Zone ID ending in .0)
    printf("  > Parseando 'r.007.0' (Identificador de zona, ilegal)...\n");
    assert(lna_from_str("r.007.0", &parsed_addr) == -1);
    printf("  > OK: Parseo rechazado por seguridad.\n");

    // Invalid node (Broadcast ending in .F)
    printf("  > Parseando 'r.007.F' (Broadcast, ilegal para asignar)...\n");
    assert(lna_from_str("r.007.F", &parsed_addr) == -1);
    printf("  > OK: Parseo rechazado por seguridad.\n");

    printf(" -> TEST 1 SUPERADO.\n\n");

    /* ------------------------------------------------------------
     * TEST 2: Serialization and 12B Header alignment
     * ------------------------------------------------------------ */
    printf("[TEST 2] Verificando empaquetado de trama...\n");
    lnp_frame_t tx_frame, rx_frame;
    uint8_t wire_buffer[255];
    const char *mock_payload = "TEMP:24.5;HUM:68";
    uint8_t payload_len = (uint8_t)strlen(mock_payload);

    lna_t addr_leaf = lna_make(LNP_TYPE_LEAF, 2, 1);        // l.002.1
    lna_t addr_gw   = lna_make(LNP_TYPE_GATEWAY, 0x3FF, 1); // g.3FF.1

    lnp_frame_init(&tx_frame, LNP_FRAME_DATA, addr_leaf, addr_gw, 8, 42);
    assert(lnp_frame_set_payload(&tx_frame, (const uint8_t *)mock_payload, payload_len));

    uint16_t wire_bytes = lnp_frame_serialize(&tx_frame, wire_buffer, sizeof(wire_buffer));
    printf("  > Trama serializada en %u bytes (12B cabecera + %d payload).\n", wire_bytes, payload_len);
    assert(wire_bytes == (LNP_HEADER_SIZE + payload_len));

    // Check critical header fields
    assert(wire_buffer[1] == payload_len);
    assert(wire_buffer[2] == 8);
    assert(wire_buffer[3] == 42);

    assert(lnp_frame_deserialize(&rx_frame, wire_buffer, wire_bytes));
    assert(rx_frame.header.len == payload_len);
    assert(memcmp(rx_frame.payload, mock_payload, payload_len) == 0);

    printf(" -> TEST 2 SUPERADO.\n\n");

    /* ------------------------------------------------------------
     * TEST 3: Network setup and Mesh discovery
     * ------------------------------------------------------------ */
    printf("[TEST 3] Construyendo topologia: Gateway <-> Root 1 <-> Root 2\n");
    lnp_router_t router_gw, router_r1, router_r2;

    lna_t addr_r1 = lna_make(LNP_TYPE_ROOT, 1, 1); // r.001.1
    lna_t addr_r2 = lna_make(LNP_TYPE_ROOT, 2, 1); // r.002.1

    lnp_router_init(&router_gw, addr_gw);
    lnp_router_init(&router_r1, addr_r1);
    lnp_router_init(&router_r2, addr_r2);

    // Step 1: GW Beacon
    lnp_frame_t beacon_frame;
    assert(lnp_router_build_beacon(&router_gw, &beacon_frame));
    printf("  > Gateway emite Beacon (Coste 0).\n");

    // Step 2: R1 receives beacon
    lnp_action_t act = lnp_router_route(&router_r1, &beacon_frame, current_time_ms);
    assert(act == LNP_ACTION_HANDLED);
    assert(router_r1.has_route_to_gateway == true);
    assert(router_r1.current_cost == 1);
    printf("  > Root 1 enganchado. Padre: g.3FF.1, Coste: 1.\n");

    // Step 3: R1 builds its own beacon
    assert(lnp_router_build_beacon(&router_r1, &beacon_frame));
    printf("  > Root 1 retransmite Beacon (Coste 1).\n");

    // Step 4: R2 receives R1's beacon
    act = lnp_router_route(&router_r2, &beacon_frame, current_time_ms);
    assert(act == LNP_ACTION_HANDLED);
    assert(router_r2.has_route_to_gateway == true);
    assert(router_r2.current_cost == 2);
    printf("  > Root 2 enganchado. Padre: r.001.1, Coste: 2.\n");

    printf(" -> TEST 3 SUPERADO.\n\n");

    /* ------------------------------------------------------------
     * TEST 4: Multi-hop uplink traffic simulation
     * ------------------------------------------------------------ */
    printf("[TEST 4] Transmitiendo lectura desde Leaf hacia el Gateway...\n");
    lnp_frame_t flight_frame;

    lnp_frame_init(&flight_frame, LNP_FRAME_DATA, addr_leaf, addr_gw, 8, 1);
    flight_frame.header.next_hop = addr_r2;
    assert(lnp_frame_set_payload(&flight_frame, (const uint8_t *)"DATA", 4));

    print_trace("LEAF TX  ", flight_frame.header.src, flight_frame.header.prev_hop,
                flight_frame.header.next_hop, flight_frame.header.dst, flight_frame.header.ttl);

    // Hop 1: R2 to R1
    act = lnp_router_route(&router_r2, &flight_frame, current_time_ms);
    assert(act == LNP_ACTION_FORWARD);
    assert(lna_eq(flight_frame.header.next_hop, addr_r1));
    print_trace("ROOT2 FWD", flight_frame.header.src, flight_frame.header.prev_hop,
                flight_frame.header.next_hop, flight_frame.header.dst, flight_frame.header.ttl);

    // Hop 2: R1 to GW
    act = lnp_router_route(&router_r1, &flight_frame, current_time_ms);
    assert(act == LNP_ACTION_FORWARD);
    assert(lna_eq(flight_frame.header.next_hop, addr_gw));
    print_trace("ROOT1 FWD", flight_frame.header.src, flight_frame.header.prev_hop,
                flight_frame.header.next_hop, flight_frame.header.dst, flight_frame.header.ttl);

    // Arrival at GW
    act = lnp_router_route(&router_gw, &flight_frame, current_time_ms);
    assert(act == LNP_ACTION_CONSUME);
    print_trace("GW RX    ", flight_frame.header.src, flight_frame.header.prev_hop,
                flight_frame.header.next_hop, flight_frame.header.dst, flight_frame.header.ttl);

    printf(" -> TEST 4 SUPERADO.\n\n");

    /* ------------------------------------------------------------
     * TEST 5: Duplicate packet suppression (Dedup)
     * ------------------------------------------------------------ */
    printf("[TEST 5] Verificando bloqueo de paquetes duplicados (dedup)...\n");
    printf("  > Reinyectando el mismo paquete en Root 1...\n");

    // Adjust next_hop so MAC filter lets it through to the dedup module
    flight_frame.header.next_hop = addr_r1;

    act = lnp_router_route(&router_r1, &flight_frame, current_time_ms);
    assert(act == LNP_ACTION_DROP);
    printf("  > Paquete descartado correctamente por dedup.\n");

    printf(" -> TEST 5 SUPERADO.\n\n");

    /* ------------------------------------------------------------
     * TEST 6: Lazy Leaf node power saving
     * ------------------------------------------------------------ */
    printf("[TEST 6] Comprobando que el nodo Leaf ahorra bateria (no beacons, no forwarding)...\n");

    lnp_router_t router_flojo;
    lna_t addr_flojo = lna_make(LNP_TYPE_LEAF, 4, 1); // l.004.1
    lnp_router_init(&router_flojo, addr_flojo);

    // Test 6.1: Beacon generation refusal
    lnp_frame_t test_beacon;
    router_flojo.has_route_to_gateway = true; // Fake route to bait it
    bool hace_beacon = lnp_router_build_beacon(&router_flojo, &test_beacon);
    assert(hace_beacon == false);
    printf("  > OK: El leaf se niega a enviar beacons.\n");

    // Test 6.2: Forwarding refusal
    lnp_frame_t test_fwd;
    lna_t addr_origen = lna_make(LNP_TYPE_ROOT, 9, 1); // r.009.1

    lnp_frame_init(&test_fwd, LNP_FRAME_DATA, addr_origen, addr_gw, 8, 10);
    test_fwd.header.next_hop = addr_flojo; // "Hey leaf, forward this for me"

    lnp_action_t act_flojo = lnp_router_route(&router_flojo, &test_fwd, current_time_ms);
    assert(act_flojo == LNP_ACTION_DROP);
    printf("  > OK: El leaf descarta trafico ajeno para dormir.\n");

    printf(" -> TEST 6 SUPERADO.\n\n");

    printf("====================================================\n");
    printf("      BOMBA: TODOS LOS TESTS HAN PASADO MIARMA      \n");
    printf("====================================================\n");

    return 0;
}
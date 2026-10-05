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

static void print_trace(const char *step, lna_t src, lna_t prev, lna_t next, lna_t dst, uint8_t ttl)
{
    char s_src[16], s_prev[16], s_next[16], s_dst[16];
    lna_to_str(src, s_src, sizeof(s_src));
    lna_to_str(prev, s_prev, sizeof(s_prev));
    lna_to_str(next, s_next, sizeof(s_next));
    lna_to_str(dst, s_dst, sizeof(s_dst));

    printf("[%s] %s -> %s (Link: %s -> %s) | TTL: %u\n",
           step, s_src, s_dst, s_prev, s_next, ttl);
}

int main(void)
{
    printf("====================================================\n");
    printf("        INICIANDO SIMULADOR DE PROTOCOLO LNPv1      \n");
    printf("====================================================\n\n");

    uint32_t current_time_ms = 1000;

    /* ------------------------------------------------------------
     * TEST 1: Direccionamiento estricto y Formato
     * ------------------------------------------------------------ */
    printf("[TEST 1] Verificando parseo y formateo de direcciones LNA...\n");
    lna_t parsed_addr;
    assert(lna_from_str("r.007.0", &parsed_addr) == 0);
    assert(lna_type(parsed_addr) == LNP_TYPE_ROOT);
    assert(lna_zone(parsed_addr) == 7);
    assert(lna_node(parsed_addr) == 0);

    char str_buf[16];
    lna_to_str(parsed_addr, str_buf, sizeof(str_buf));
    assert(strcmp(str_buf, "r.007.0") == 0);
    printf(" -> Direcciones operativas al 100%%.\n\n");

    /* ------------------------------------------------------------
     * TEST 2: Serialización con nuevo alineamiento (12B Header)
     * ------------------------------------------------------------ */
    printf("[TEST 2] Verificando empaquetado de trama en memoria...\n");
    lnp_frame_t tx_frame, rx_frame;
    uint8_t wire_buffer[255];
    const char *mock_payload = "TEMP:24.5;HUM:68";
    uint8_t payload_len = (uint8_t)strlen(mock_payload);

    lna_t addr_leaf = lna_make(LNP_TYPE_LEAF, 2, 1);       // l.002.1
    lna_t addr_gw   = lna_make(LNP_TYPE_GATEWAY, 0x3FF, 1); // g.3FF.1

    lnp_frame_init(&tx_frame, LNP_FRAME_DATA, addr_leaf, addr_gw, 8, 42);
    assert(lnp_frame_set_payload(&tx_frame, (const uint8_t *)mock_payload, payload_len));

    uint16_t wire_bytes = lnp_frame_serialize(&tx_frame, wire_buffer, sizeof(wire_buffer));
    assert(wire_bytes == (LNP_HEADER_SIZE + payload_len));

    // Verificamos bytes críticos del nuevo orden de cabecera
    assert(wire_buffer[1] == payload_len); // Length en buffer[1]
    assert(wire_buffer[2] == 8);           // TTL en buffer[2]
    assert(wire_buffer[3] == 42);          // Seq en buffer[3]

    assert(lnp_frame_deserialize(&rx_frame, wire_buffer, wire_bytes));
    assert(rx_frame.header.len == payload_len);
    assert(memcmp(rx_frame.payload, mock_payload, payload_len) == 0);
    printf(" -> Serialización y deserialización correcta (%u bytes transmitidos).\n\n", wire_bytes);

    /* ------------------------------------------------------------
     * TEST 3: Configuración de la Red y Descubrimiento Mesh
     * ------------------------------------------------------------ */
    printf("[TEST 3] Construyendo topologia Mesh: Gateway <-> Root 1 <-> Root 2 <-> Leaf 2\n");
    lnp_router_t router_gw, router_r1, router_r2;

    lna_t addr_r1 = lna_make(LNP_TYPE_ROOT, 1, 0); // r.001.0
    lna_t addr_r2 = lna_make(LNP_TYPE_ROOT, 2, 0); // r.002.0

    lnp_router_init(&router_gw, addr_gw);
    lnp_router_init(&router_r1, addr_r1);
    lnp_router_init(&router_r2, addr_r2);

    // 1. Gateway emite Beacon (Coste 0)
    lnp_frame_t beacon_frame;
    assert(lnp_router_build_beacon(&router_gw, &beacon_frame));

    // 2. Root 1 escucha al Gateway
    lnp_action_t act = lnp_router_route(&router_r1, &beacon_frame, current_time_ms);
    assert(act == LNP_ACTION_HANDLED);
    assert(router_r1.has_route_to_gateway == true);
    assert(router_r1.current_cost == 1);
    assert(lna_eq(router_r1.default_gateway, addr_gw));
    printf(" -> Root 1 enganchado al Gateway a 1 salto.\n");

    // 3. Root 1 retransmite el Beacon
    assert(lnp_router_build_beacon(&router_r1, &beacon_frame));

    // 4. Root 2 escucha a Root 1
    act = lnp_router_route(&router_r2, &beacon_frame, current_time_ms);
    assert(act == LNP_ACTION_HANDLED);
    assert(router_r2.has_route_to_gateway == true);
    assert(router_r2.current_cost == 2);
    assert(lna_eq(router_r2.default_gateway, addr_r1));
    printf(" -> Root 2 enganchado a Root 1 a 2 saltos.\n\n");

    /* ------------------------------------------------------------
     * TEST 4: Simulación de Tráfico Uplink Multi-Salto
     * ------------------------------------------------------------ */
    printf("[TEST 4] Transmitiendo lectura de sensor desde Leaf 2 hacia el Gateway...\n");
    lnp_frame_t flight_frame;

    // Leaf 2 genera el paquete
    lnp_frame_init(&flight_frame, LNP_FRAME_DATA, addr_leaf, addr_gw, 8, 1);
    flight_frame.header.next_hop = addr_r2; // El satélite lo manda a su root
    assert(lnp_frame_set_payload(&flight_frame, (const uint8_t *)"DATA", 4));

    print_trace("LEAF TX  ", flight_frame.header.src, flight_frame.header.prev_hop,
                flight_frame.header.next_hop, flight_frame.header.dst, flight_frame.header.ttl);

    // Salto 1: Root 2 procesa y enruta
    act = lnp_router_route(&router_r2, &flight_frame, current_time_ms);
    assert(act == LNP_ACTION_FORWARD);
    assert(lna_eq(flight_frame.header.next_hop, addr_r1)); // Se lo pasa a R1
    print_trace("ROOT2 FWD", flight_frame.header.src, flight_frame.header.prev_hop,
                flight_frame.header.next_hop, flight_frame.header.dst, flight_frame.header.ttl);

    // Salto 2: Root 1 procesa y enruta
    act = lnp_router_route(&router_r1, &flight_frame, current_time_ms);
    assert(act == LNP_ACTION_FORWARD);
    assert(lna_eq(flight_frame.header.next_hop, addr_gw)); // Se lo pasa al Gateway
    print_trace("ROOT1 FWD", flight_frame.header.src, flight_frame.header.prev_hop,
                flight_frame.header.next_hop, flight_frame.header.dst, flight_frame.header.ttl);

    // Salto 3: Llegada a Gateway
    act = lnp_router_route(&router_gw, &flight_frame, current_time_ms);
    assert(act == LNP_ACTION_CONSUME);
    print_trace("GW RX    ", flight_frame.header.src, flight_frame.header.prev_hop,
                flight_frame.header.next_hop, flight_frame.header.dst, flight_frame.header.ttl);
    printf(" -> Paquete recibido con exito en el Gateway.\n\n");

    /* ------------------------------------------------------------
     * TEST 5: Control de Duplicados (Deduplicación)
     * ------------------------------------------------------------ */
    printf("[TEST 5] Verificando bloqueo de paquetes duplicados...\n");
    // Simulamos que a Root 1 le llega el mismo paquete exacto otra vez por eco
    act = lnp_router_route(&router_r1, &flight_frame, current_time_ms);
    assert(act == LNP_ACTION_DROP);
    printf(" -> Duplicado detectado y descartado correctamente.\n\n");

    printf("====================================================\n");
    printf("      TODOS LOS TESTS HAN PASADO EXITOSAMENTE       \n");
    printf("====================================================\n");

    return 0;
}
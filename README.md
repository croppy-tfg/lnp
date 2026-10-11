# LNPv1

5 de octubre de 2026 a las 9:00


---

El protocolo **LNP** está diseñado para redes inalámbricas de sensores de bajo consumo. Provee de un formato de trama, dirección y encaminamiento detallados a continuación:

## Trama


---

![](https://cdn.miarma.net/files/img/lnpv1-frame.png "left-50 =642x282")

* Versión: 4 bits (actualmente `1`)
* Tipo: 4 bits
* Length: 1B
* TTL: 1B
* SN: 1B
* Source address: 2B
* Destination address: 2B
* Previous hop: 2B
* Next hop: 2B

La cabecera ocupa **12B**, así que en LoRa (255B máximo) quedan **243B** de payload. Los campos de 2B viajan en *little-endian*.

* **Previous hop**: el nodo que transmite la trama en este salto. Sirve para aprender el camino de vuelta.
* **Next hop**: el único nodo que debe procesar la trama. El resto la ignora.
* **SN**: lo incrementa el origen en cada trama. El par (origen, SN) sirve para descartar duplicados.

### Tipos de trama

* `0` → Datos
* `1` → Control (*beacon frames → tramas baliza*)

## Dirección


---

| Tipo\* | Zona | Nodo |
|:-----:|:----:|:----:|
| 2 bits | 10 bits | 4 bits |

\*aunque en protocolos tipo IPv4 valores como el tipo (de dispositivo en este caso) se identificarían en la cabecera, en este protocolo se identifican en la propia dirección para filtrar el tráfico ajeno en un sólo ciclo de CPU, reduciendo el consumo de batería.

### Tipos

* `00` → Gateway ( ‘g’ )
* `01` → Root ( ‘r’ )
* `10` → Leaf ( ‘l’ )
* `11` → Reservado ( ‘#’ )

### Zona y nodos

Hay espacio hasta para 1024 zonas distintas, cada una con hasta 16 nodos distintos. El nodo `0` identifica la zona y el `F` es broadcast, así que quedan 14 asignables (`1` - `E`).

## Formato


---

| Ejemplo | Base 2 | Formato | Asignable |
|---------|--------|---------|-----------|
| **Identificador** de zona 9 | `1100 0000 1001 0000` | `#.009.0` | `*.009.1` - `*.009.E` → 14 nodos |
| Un nodo root en la zona 7 con ID 1 | `0100 0000 0111 0001` | `r.007.1` | -         |
| Un nodo gateway **sin zona** con ID 1 | `0011 1111 1111 0001` | `g.3FF.1` | -         |
| Sin dirección asignada | `1100 0000 0000 0000` | `#.000.0` | -         |
| **Broadcast** **de zona** 123 | `1100 0111 1011 1111` | `#.07B.F` | `*.07B.1` - `*.07B.E` → 14 nodos |
| **Broadcast** | `1111 1111 1111 1111` | `#.3FF.F` | -         |
| Zonas asignables | -      | -       | `0x001` - `0x3FE` |

## Enrutamiento


---

La red es un árbol: todos los nodos buscan un camino hacia el **gateway**.

### Beacons

El gateway emite beacons periódicos y los nodos con ruta los repiten a sus vecinos. Un beacon es una trama de tipo `1` con destino `#.3FF.F`, TTL 1 y 5B de payload:

* Coste: 1B, saltos hasta el gateway (`255` = sin ruta)
* Seq: 2B, lo crea **solo el gateway** y los demás lo repiten tal cual
* Gateway ID: 2B, dirección del gateway raíz del árbol

Reglas de cada nodo:

* Elige como padre al vecino que le dé un coste **estrictamente menor** (con empate no cambia).
* Solo acepta información **más nueva** (seq mayor). Así un beacon antiguo nunca crea bucles.
* Si no oye a su padre en **5 min**, pierde la ruta y avisa una vez con coste `255` para que sus hijos la pierdan también.

### Reenvío

Al recibir una trama, un nodo hace esto en orden:


1. Es un beacon → se procesa y no se reenvía. Si es de un tipo desconocido → se descarta.
2. El Next hop no soy yo → se ignora (LoRa es un medio compartido).
3. Es un duplicado (origen, SN) → se descarta.
4. Aprende el camino de vuelta: el origen se alcanza por el Previous hop.
5. El destino soy yo (o es multicast) → se entrega.
6. TTL ≤ 1 → se descarta. Si no, se resta 1.
7. Elige el siguiente salto, en este orden:
   * Ruta aprendida hacia el destino
   * Misma zona → directo al destino
   * Hacia el padre (gateway)
   * Sin ruta → se descarta

Una trama nunca se devuelve al nodo del que vino.

### Rutas hacia abajo

Las rutas hacia los nodos se aprenden al reenviar tráfico (paso 4), así que el gateway solo conoce a un nodo si este ha transmitido algo. Los nodos deben enviar datos periódicamente.

### Parámetros

| Parámetro | Valor |
|-----------|-------|
| TTL por defecto | 8     |
| Timeout de beacon | 5 min |
| Caducidad de rutas aprendidas | 60 min |
| Rutas aprendidas por nodo | 128   |
| Caché de duplicados | 16    |
| Caducidad de duplicados | 10 min |

## Limitaciones


---

* Se asume que todos los nodos de una misma zona se oyen entre sí (todos los de una zona se instalan lo suficientemente cerca físicamente).
* Todavía no hay cifrado ni autenticación.



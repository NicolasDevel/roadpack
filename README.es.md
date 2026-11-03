# roadpack

[![tests](https://github.com/NicolasDevel/roadpack/actions/workflows/tests.yml/badge.svg)](https://github.com/NicolasDevel/roadpack/actions/workflows/tests.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Python 3.10+](https://img.shields.io/badge/python-3.10%2B-blue.svg)

**Navegación giro a giro en un microcontrolador, sin conexión, para un país entero.**

roadpack compila un extracto de OpenStreetMap en un `.gmap`: un grafo vial binario y compacto con una
**jerarquía de contracción** precalculada, organizado para que un dispositivo con unos pocos MB de RAM rutee
leyendo directo de una microSD, sin cargar nunca el mapa. Toda Colombia ocupa 256 MB, con las restricciones de
giro, y un ESP32-S3 encuentra la ruta de 963 km de Bogotá a Cartagena en menos de 3 segundos, con la caché vacía ([en el dispositivo](#en-el-dispositivo)).

[Read in English](README.md)

![El grafo vial completo de Colombia con la ruta Bogotá–Cartagena y tres pantallas de maniobra de la pantalla redonda](docs/img/hero.png)

## Por qué

Motores de ruteo como OSRM, Valhalla o GraphHopper están hechos para servidores y teléfonos, con sistema
operativo, sistema de archivos y memoria de sobra. Un microcontrolador tiene unos pocos MB de RAM, una tarjeta SD
en FAT que lee unos pocos MB por segundo y ningún sistema operativo. roadpack no es un motor de ruteo: es el paso
de compilación que produce un mapa sobre el que ese dispositivo puede rutear.

- **Solo lo que el ruteo necesita.** Las vías por las que puede ir el vehículo; nodos del grafo solo en los
  cruces y los finales de vía (del 13 al 47 % de los nodos de vías de OSM); formas simplificadas a 2 m.
- **Pocos nodos por ruta.** La jerarquía de contracción asienta unos cientos de nodos para cruzar un país, donde
  A* asienta cientos de miles (Colombia: 275 contra 458.000, mediana de 200 rutas al azar).
- **Se lee en el sitio.** Registros de tamaño fijo en little-endian, nodos en orden de curva Z para que las vías
  cercanas queden en las mismas páginas de la SD, un índice espacial disperso para ubicar la posición del GPS, y
  el archivo partido en pedazos de 4 MiB porque FAT sin *fast seek* es lento con archivos grandes.
- **Verificado.** La jerarquía coincide con A* en 2.200 rutas al azar de 22 regiones, y la salida del generador
  se compara con la implementación de referencia original ([cómo](docs/SCALING.md), en inglés).

Nació para un navegador de moto sin conexión, y funciona en él: una Waveshare ESP32-S3 con pantalla AMOLED
redonda de 466×466 que lee el mapa de una microSD.

## Inicio rápido

```bash
pip install git+https://github.com/NicolasDevel/roadpack.git

# Cualquier extracto de https://download.geofabrik.de o https://extract.bbbike.org
roadpack build colombia-latest.osm.pbf -o out/colombia.gmap                 # el extracto completo
roadpack build colombia-latest.osm.pbf --region bogota -o out/bogota.gmap   # o --bbox minlat,minlon,maxlat,maxlon

roadpack info  out/bogota.gmap
roadpack route out/bogota.gmap --from 4.5981,-74.0760 --to 4.6766,-74.0482 --check --images out/preview
roadpack stats out/bogota.gmap --routes 200
```

```
Route: 13.9 km, ~19 min, 155 edges, 11 maneuvers
  contraction hierarchy settled 293 nodes in 1 ms (Python)
  A* on the full graph settled 19,263 nodes in 30 ms: same cost

    0.00 km  (+    0 m)  Head out onto Calle 11
    0.55 km  (+  550 m)  Turn left onto Avenida Calle 7
    0.67 km  (+  120 m)  Turn left onto Carrera 5
   ...
```

- `build` escribe el `.gmap` y el mismo archivo partido en pedazos para la SD (`out/bogota/0000.bin`, …).
  `--profile car` aplica las reglas de acceso y de sentido de los carros en lugar de las de las motos.
- `route` rutea como lo haría el dispositivo e imprime las maniobras; `--check` compara con A* sobre el grafo
  completo y `--images` dibuja la ruta y la pantalla de cada maniobra.
- `stats` rutea entre pares de nodos al azar y reporta los nodos asentados y los KB del archivo que lee cada ruta,
  que es lo que la SD tiene que entregar.

La primera construcción descarga [RoutingKit](https://github.com/RoutingKit/RoutingKit) y compila un ayudante
pequeño en `~/.cache/roadpack`: hacen falta `git` y un compilador de C++17.

## Cómo funciona

```mermaid
flowchart LR
    pbf[".osm.pbf"] --> load["Carga<br/>vías highway, luego sus nodos<br/>(dos pasadas, filtradas en C++)"]
    load --> graph["Grafo<br/>partido en los cruces<br/>formas simplificadas · curva Z<br/>índice espacial<br/>restricciones de giro"]
    graph --> ch["Jerarquía de contracción<br/>(RoutingKit)"]
    ch --> gmap[".gmap<br/>+ pedazos de 4 MiB"]
    gmap --> sd["microSD"] --> mcu["Microcontrolador<br/>Dijkstra bidireccional hacia arriba<br/>con una caché de páginas pequeña"]
```

```
┌──────────┬────────────┬──────────┬─────────────────┬────────────┬─────────┬────────────┬─────────────┬─────────┐
│ cabecera │ nodos      │ aristas  │ índice espacial │ geometría  │ nombres │ CH forward │ CH backward │ copias  │
│ 80 B     │ 12 B × N+1 │ 28 B × E │ disperso        │ variable   │ UTF-8   │            │             │ 8 B × K │
└──────────┴────────────┴──────────┴─────────────────┴────────────┴─────────┴────────────┴─────────────┴─────────┘
```

El dispositivo responde una consulta corriendo Dijkstra desde los dos extremos, subiendo siempre en la
jerarquía, y luego desarma los atajos en aristas reales. El formato, byte por byte, y la consulta están en
[FORMAT.md](FORMAT.md) (en inglés); el lector de Python en `roadpack.format` y el ruteo en `roadpack.routing` son
la implementación de referencia.

## Resultados

De los [benchmarks](benchmarks/README.md) (en inglés): perfil de moto, 200 rutas al azar por región, en un
portátil de 16 GB.

| Región | Nodos del grafo | `.gmap` | Construcción | Ruta mediana | Nodos asentados (mediana) | A* asienta | KB leídos por consulta |
|---|---:|---:|---:|---:|---:|---:|---:|
| Bogotá | 91 mil | 15 MB | 5 s | 15 km | 272 | 29 mil | 1.072 |
| Londres | 419 mil | 62 MB | 11 s | 31 km | 389 | 83 mil | 2.040 |
| Tokio | 236 mil | 42 MB | 7 s | 17 km | 462 | 81 mil | 2.112 |
| Colombia | 1,27 M | 263 MB | 54 s | 610 km | 294 | 439 mil | 2.972 |
| España | 3,41 M | 603 MB | 2 min | 563 km | 420 | 545 mil | 3.560 |
| California | 2,94 M | 505 MB | 95 s | 408 km | 566 | 1,09 M | 4.496 |
| Brasil | 10,2 M | 2,2 GB | 8 min | 1.674 km | 693 | 3,59 M | 8.020 |

Los benchmarks también incluyen Ciudad de México, Lima, Buenos Aires, Santiago, Washington DC, Ottawa, Toronto,
Nueva York, Madrid, Barcelona, París, Berlín, Canadá, Texas, el estado de Nueva York y Colombia por departamento.
[Cómo caben los países grandes en un portátil, y la evidencia de que los mapas son exactos](docs/SCALING.md).

## En el dispositivo

Medido en el navegador para el que se hizo: un ESP32-S3 con una caché de páginas de 2 MB en PSRAM (páginas de
4 KiB), leyendo el mapa de Colombia (v3, con restricciones de giro; datos de OSM del 27-10-2026) de una microSD en
modo de 1 bit a 40 MHz, con la caché vacía antes de cada ruta. Mediana de 10 corridas.

| Ruta | Distancia | Ruta encontrada | Lista para navegar | Lecturas de la SD |
|---|---:|---:|---:|---:|
| Bogotá de lado a lado | 15 km | 0,9 s | 1,4 s | 423 |
| Bogotá – San Francisco | 62 km | 1,0 s | 1,4 s | 464 |
| Bogotá – Medellín | 427 km | 2,0 s | 2,5 s | 839 |
| Bogotá – Cartagena | 963 km | 2,9 s | 3,4 s | 1.139 |

*Ruta encontrada* es la consulta en la jerarquía más desarmar los atajos en aristas. *Lista para navegar* suma las
formas y maniobras de los primeros 15 km.

Lo que hizo la diferencia:

- **El tiempo se va en esperar a la tarjeta.** Cada lectura al azar de 4 KiB cuesta unos 2,8 ms y el cálculo casi
  no pesa, así que lo que hay que mirar son las lecturas por ruta (lo que reporta `roadpack stats`). Las páginas
  más grandes fueron más lentas: el costo crece con los bytes leídos.
- **Armar las formas sobre la marcha.** Leer las formas y revisar cada cruce de una ruta de 963 km cuesta más que
  encontrarla (1.352 lecturas), así que el navegador solo arma 15 km por delante. Bogotá–Cartagena pasó de 8,2 a
  4,4 s. Sumar el largo del resto de la ruta en marcha, y no antes de mostrarla, ahorró otras ~400 lecturas:
  mientras tanto, la distancia que falta se estima con el tiempo de recorrido de las aristas.
- **Usar una tarjeta original.** Una falsificada (vendida como de marca conocida, pero su CID dice fabricante 0x6F)
  se frenaba unos 9 s en una de cada tres rutas y a veces fallaba una lectura aun con reintentos. Una lectura
  fallida deja ceros donde iban los datos, sin avisar, así que el dispositivo las cuenta y vuelve a calcular la
  ruta. La misma tarjeta guardó una vez una copia del mapa con 16 KB de un pedazo cambiados por otros datos, sin
  ningún error: hay que comparar los archivos después de copiarlos a la tarjeta.

## Límites

- El formato `.gmap` direcciona 4 GiB y unos 164° de longitud: Brasil cabe (2,2 GB); Estados Unidos completo
  no, así que se construye por estados.
- Construir necesita al menos unos 1,5 GB de RAM, y unos 9 GB para Brasil.
- Se aplican las restricciones de giro con un nodo o con vías como `via` (en Colombia, 6.826 de 6.940 en el
  extracto de octubre de 2026); las que dependen del horario, no. Las velocidades salen de `maxspeed` o de un valor
  por defecto según el tipo de vía (sin tráfico).
- El lado del microcontrolador está especificado en FORMAT.md, con dos implementaciones: la de referencia en Python y
  un lector y router en C99 portable en [reader/](reader/README.md) (sin memoria dinámica, comparado con el de
  Python). Las maniobras por ahora solo están en Python.

## Organización del proyecto

| Módulo | Capa |
|---|---|
| `roadpack.osm` | Carga: vías de un extracto de OSM aptas para un perfil de vehículo (`motorcycle`, `car`) |
| `roadpack.graph` | Grafo: tramos entre cruces, geometría simplificada, curva Z, índice espacial (numpy) |
| `roadpack.ch` | Jerarquía de contracción (RoutingKit, `build_ch.cpp`) |
| `roadpack.format` | El formato `.gmap`: escritor, lector y pedazos para la SD |
| `roadpack.routing` | Ruteo de referencia: consulta con la CH, A*, maniobras giro a giro |
| `roadpack.stats`, `roadpack.preview` | Muestreo de rutas; PNG de una ruta y de la pantalla de cada maniobra |
| `benchmarks/` | Scripts de benchmarks, validación y comparación con la referencia, con sus resultados |
| `reader/` | Lector y router en C99 para microcontroladores ([reader/README.md](reader/README.md), en inglés) |

```bash
pip install -e '.[test]' && pytest
```

Las pruebas construyen archivos de OpenStreetMap sintéticos y pequeños (una ciudad hecha a mano que ejercita cada
regla, y cuadrículas de calles al azar) y verifican, entre otras cosas, que la jerarquía de contracción coincida
con Dijkstra en todos los pares de nodos.

## Licencias

- El código de este repositorio: [MIT](LICENSE).
- Los mapas generados son una base de datos derivada de OpenStreetMap, © colaboradores de OpenStreetMap, bajo la
  [ODbL](https://opendatacommons.org/licenses/odbl/): si se distribuyen, hay que dar esa atribución y compartirlos
  bajo la misma licencia.
- [RoutingKit](https://github.com/RoutingKit/RoutingKit) (BSD 2-Clause) se descarga y compila al usarlo; no se
  incluye en este repositorio.
- Dependencias de Python: [pyosmium](https://github.com/osmcode/pyosmium) (BSD 2-Clause),
  [NumPy](https://numpy.org) (BSD 3-Clause), [Pillow](https://python-pillow.org) (MIT-CMU).

# webpad-esp32

Emula un mando de PS4 (DualShock 4) por USB usando un ESP32-S3, y lo controlas desde un navegador web — el propio ESP32 hostea una página con un mando virtual interactivo, joysticks arrastrables y touchpad táctil.

Pensado para situaciones donde necesitas un mando "de emergencia": una PS4 recién reseteada que pide pulsar PS para emparejar, debugging, accesibilidad, automatización, etc.

## Cómo funciona

```
   ┌────────────┐    USB OTG    ┌─────────┐
   │  ESP32-S3  │ ────────────► │   PS4   │
   │            │   (HID DS4)   │   PS5   │
   └─────┬──────┘                └─────────┘
         │ WiFi
         ▼
   ┌────────────┐
   │ Navegador  │  ◄── http://<ip-del-esp>:7777
   │ (móvil/PC) │
   └────────────┘
```

El ESP32-S3 se presenta a la consola como un mando licenciado oficialmente (VID/PID de HORI Fighting Commander OCTA en modo PS5). Simultáneamente arranca un servidor WiFi que sirve una página con un DualShock 4 dibujado en SVG. Cuando pulsas un botón en la web, el ESP32 envía el report HID correspondiente a la consola por USB.

## Características

- **HID nativo USB**: aparece como mando licenciado, no requiere drivers
- **Mando virtual web**: SVG real del DS4 con hitboxes pixel-perfect detectadas automáticamente
- **Joysticks arrastrables**: arrastra los sticks con el ratón o el dedo, vuelve al centro al soltar
- **Touchpad táctil**: arrastra sobre el touchpad para simular swipes
- **L3 / R3**: tap rápido sobre un stick = clic del joystick
- **Atajos de teclado**: ESC=○, Espacio=✕, flechas=dpad, etc.
- **Compatible con PS4 reseteada de fábrica** (setup inicial sin mando físico)
- **Sin claves Sony**: usa stub de autenticación → funciona ~8 minutos por sesión

## Limitaciones

- **PS5**: la auth RSA es estricta, la enumeración suele fallar tras unos segundos
- **PS4**: funciona, pero la consola desconecta el mando a los ~8 minutos por timeout de auth (es suficiente para configurar la PS4; reconecta el cable para otros 8 minutos)
- Sin claves RSA reales de Sony no se puede pasar la auth indefinidamente. Si quieres soporte permanente, GP2040-CE permite subir claves dumpeadas de un DS4 real

## Hardware necesario

- **ESP32-S3** con USB nativo y conector USB-A o USB-C OTG separado del USB de programación. Probado con **ESP32-S3-N16R8** (16 MB flash, 8 MB PSRAM, dos puertos USB-C: uno UART/COM, otro USB/OTG)
- Cable USB-A o USB-C macho-macho para conectar el ESP32 a la consola
- Red WiFi 2.4 GHz (el ESP32-S3 no soporta 5 GHz)

> **Importante**: tu placa debe tener **dos puertos USB físicamente separados**. Uno suele estar marcado `COM`/`UART` (lo usas para programar) y el otro `USB`/`OTG` (lo conectas a la consola). Si solo tiene un puerto USB compartido, no funcionará.

## Software necesario

- [ESP-IDF v5.0+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html) (probado con v6.1-dev)
- Python 3.10+
- Drivers USB-Serial para tu placa (CH343, CP210x o similar — normalmente ya instalados)

## Instalación

### 1. Instalar ESP-IDF

Sigue [esta guía oficial](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html) para tu sistema operativo. En Linux:

```bash
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

### 2. Clonar este repo

```bash
git clone https://github.com/pepegocar14/webpad-esp32.git
cd webpad-esp32
```

### 3. Configurar tu WiFi

Tienes dos opciones:

**Opción A — Menuconfig (recomendado)**:

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

Ve a `PS4 Controller Config` → introduce tu SSID y contraseña → guarda (S) y sal (Q).

**Opción B — Archivo `sdkconfig.local`** (no se commitea):

```bash
cat > sdkconfig.local <<EOF
CONFIG_WIFI_SSID="mi_red_wifi"
CONFIG_WIFI_PASSWORD="mi_contraseña"
EOF
```

### 4. Compilar y flashear

Conecta el ESP32 por el **puerto COM/UART** (no el OTG) al PC y:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Reemplaza `/dev/ttyACM0` por tu puerto (`/dev/ttyUSB0` en Linux con CH340, `COMx` en Windows).

Cuando arranque, verás en el monitor algo como:

```
I (6505) MAIN: IP: 192.168.1.42
I (6505) MAIN: Abre http://192.168.1.42:7777 en tu navegador
I (6534) WEB: Servidor web en puerto 7777
```

## Uso

1. **Desconecta el cable COM/UART** (ya no lo necesitas)
2. Conecta el **puerto USB/OTG** del ESP32 a un puerto USB de la PS4 / PS5
3. Abre la URL que te dio el monitor en cualquier navegador (móvil, PC, tablet) conectado a la misma red WiFi
4. Pulsa el botón **PS** en la web — la consola debería detectar el mando y emparejarlo

### Controles web

| Acción                    | Cómo |
|---------------------------|------|
| Botón normal              | Click / tap |
| Joystick                  | Arrastra el círculo del stick |
| L3 / R3                   | Tap rápido sobre el stick (sin arrastrar) |
| Touchpad swipe            | Arrastra sobre el touchpad |
| Touchpad click            | Click / tap (el botón touchpad se mantiene durante el arrastre) |

### Atajos de teclado

| Tecla | Botón |
|-------|-------|
| Espacio / Enter | ✕ (Cross) |
| Esc | ○ (Circle) |
| T | △ (Triangle) |
| S | □ (Square) |
| P | PS |
| ↑↓←→ | D-Pad |
| Q / E | L1 / R1 |
| Shift+Q / Shift+E | L2 / R2 |
| Backspace | Options |
| N | Share |

## Estructura del proyecto

```
webpad-esp32/
├── CMakeLists.txt              # Build de ESP-IDF
├── partitions.csv              # Tabla de particiones (factory 3 MB)
├── sdkconfig.defaults          # Config por defecto (WiFi placeholder)
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild       # Menú menuconfig
│   ├── idf_component.yml       # Dependencia esp_tinyusb
│   ├── main.c                  # Entry, WiFi, hid_task
│   ├── usb_hid_ds4.c/.h        # Descriptor HID DS4 + auth stub
│   ├── web_server.c/.h         # HTTP server (puerto 7777)
│   └── web/
│       └── index.html          # UI del mando virtual (embebido en flash)
└── README.md
```

## API del servidor

El servidor expone tres endpoints en el puerto 7777:

- `GET /` — sirve `index.html`
- `POST /api/press` — pulsa un botón
  ```json
  { "button": "cross", "hold": true }
  ```
  Botones válidos: `cross`, `circle`, `square`, `triangle`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`, `share`, `options`, `ps`, `touchpad`, `up`, `down`, `left`, `right`
- `POST /api/release` — suelta todos los botones y centra los sticks
- `POST /api/stick` — mueve un stick
  ```json
  { "side": "l", "x": 200, "y": 50 }
  ```
  `side` = `"l"` o `"r"`. `x` e `y` van de 0 a 255 (centro = 128).

Útil si quieres scriptear el mando desde Python, Bash o cualquier cliente HTTP.

## Cómo identificar el puerto OTG

En el ESP32-S3-N16R8 típico, los dos puertos USB-C suelen estar etiquetados:

- **`COM` o `UART`** → conectado al chip CH343/CP210x. Para programar y leer logs por puerto serie.
- **`USB` o `OTG`** → conectado directamente a los GPIO19/20 del ESP32. Para que actúe como dispositivo USB nativo (es el que va a la consola).

Si tu placa no tiene etiquetas, prueba conectando uno por uno:
- Si al conectarlo al PC aparece como dispositivo serie (`/dev/ttyACM0` o similar), es el COM/UART
- Si aparece como mando de juego (`HORI` o `DualShock`), es el OTG

## Compatibilidad

| Consola | Estado |
|---------|--------|
| PS4 reseteada (setup inicial) | ✅ Funciona ~8 min por sesión |
| PS4 ya configurada | ✅ Funciona ~8 min por sesión |
| PS5 | ⚠️ Enumera pero rechaza por auth estricta |

## Créditos y referencias

- **SVG del DS4**: [Dualshock 4 Layout.svg](https://commons.wikimedia.org/wiki/File:Dualshock_4_Layout.svg) por Tokyoship en Wikimedia Commons (CC BY 3.0)
- **HID descriptor + protocolo PS4**: estructura clonada de [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE)
- **VID/PID HORI Fighting Commander OCTA**: identificadores de un mando licenciado oficialmente por Sony
- **Auth stub**: técnica documentada en [passinglink](https://github.com/passinglink/passinglink) y [PS4 DevWiki](https://www.psdevwiki.com/ps4/DS4-USB)

## Licencia

MIT — ver [LICENSE](LICENSE).

## Disclaimer

Este proyecto es para uso personal, educativo y de accesibilidad. No incluye claves RSA propietarias de Sony y por tanto no permite uso permanente del mando emulado. Si necesitas un mando persistente, compra uno oficial o licenciado. El uso para evadir la autenticación de Sony en producción no está soportado.

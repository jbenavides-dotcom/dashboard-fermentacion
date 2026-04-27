# Firmware ESP32 — Monitor de Fermentación LP&ET

Monitor de pH y temperatura para 2 canecas de fermentación de café.
Envía datos a n8n vía HTTP POST cada 5 minutos.

---

## Hardware requerido

- 1 × ESP32 DevKit V1 (cualquier variante con WiFi)
- 2 × Módulo sensor pH PH-4502C con sonda BNC
- 2 × Sonda DS18B20 waterproof (temperatura)
- 1 × Resistencia 4.7 kΩ (pull-up 1-Wire)
- Cables Dupont, protoboard o PCB

---

## Diagrama de conexiones

```
ESP32 DevKit V1
                    ┌─────────────┐
          3.3V ─────┤ 3V3         │
           GND ─────┤ GND         │
                    │             │
  PH-4502C C-01     │             │
   Salida señal ────┤ GPIO34(ADC) │
                    │             │
  PH-4502C C-02     │             │
   Salida señal ────┤ GPIO35(ADC) │
                    │             │
  DS18B20 x2        │             │
   DQ (data) ───────┤ GPIO4       │
                    └─────────────┘

DS18B20 bus compartido:
  VCC ──────────────────────────── 3.3V
  GND ──────────────────────────── GND
  DQ  ──┬─── GPIO4 (ESP32)
        │
       4.7kΩ (pull-up)
        │
       3.3V

PH-4502C alimentación: VCC → 5V, GND → GND, Salida → GPIO34 o GPIO35
```

---

## Configuración de Arduino IDE

### 1. Instalar soporte ESP32

1. Abrir Arduino IDE 2.x
2. Ir a **File > Preferences**
3. En "Additional boards manager URLs" agregar:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
4. Ir a **Tools > Board > Boards Manager**
5. Buscar "esp32" e instalar el paquete de Espressif Systems

### 2. Instalar librerías

Ir a **Tools > Manage Libraries** y buscar/instalar:

- **OneWire** (by Paul Stoffregen) — versión 2.3.x o superior
- **DallasTemperature** (by Miles Burton) — versión 3.9.x o superior
- **ArduinoJson** (by Benoit Blanchon) — versión 6.x o 7.x

Las librerías `WiFi.h` y `HTTPClient.h` vienen incluidas con el soporte ESP32.

---

## Configuración del sketch

Abrir `fermentacion_lpet.ino` y editar las siguientes constantes al inicio:

```cpp
// Credenciales WiFi
#define WIFI_SSID     "NombreDeTuRed"
#define WIFI_PASSWORD "TuContrasenaWiFi"

// URL del webhook en n8n
#define WEBHOOK_URL "https://tu-n8n.com/webhook/fermentacion"

// Calibracion pH (ver seccion de calibracion abajo)
#define PH_VOLTAJE_4  3.00
#define PH_VOLTAJE_7  2.00
```

---

## Calibración inicial del pH

Los módulos PH-4502C tienen variaciones entre unidades. Es **obligatorio** calibrar
con 2 soluciones buffer antes de usar en producción.

### Procedimiento

1. Cargar el sketch con los valores de calibración predeterminados (3.00 y 2.00).
2. Conectar el ESP32 y abrir Monitor Serial a 115200 baud.
3. Sumergir el electrodo de C-01 en solución buffer pH 4.0.
4. Esperar 2 minutos hasta que la lectura se estabilice.
5. Anotar el voltaje que aparece en el log: `Voltaje: X.XXX V`
6. Repetir con solución buffer pH 7.0.
7. Actualizar `PH_VOLTAJE_4` y `PH_VOLTAJE_7` con los valores medidos.
8. Recompilar y cargar nuevamente.
9. Repetir el mismo proceso para el sensor de C-02 (GPIO35).

> Nota: si ambos sensores tienen valores muy distintos, calibrar por separado
> usando dos pares de constantes y pasarlas como parámetro a `leerPH()`.

---

## Cargar el sketch al ESP32

1. Conectar el ESP32 por USB.
2. En Arduino IDE, seleccionar:
   - **Board:** "ESP32 Dev Module"
   - **Port:** COMx (Windows) o /dev/ttyUSB0 (Linux/Mac)
   - **Upload Speed:** 921600
3. Click en el botón **Upload** (flecha derecha).
4. Si pide "Waiting for download mode": mantener presionado el botón **BOOT**
   del ESP32 mientras empieza la carga, soltarlo cuando aparezca "Connecting..."

---

## Verificar funcionamiento

1. Abrir **Monitor Serial** (Tools > Serial Monitor) a 115200 baud.
2. Reiniciar el ESP32 (botón RST).
3. Verificar en el log:
   - `WiFi conectado. IP: 192.168.x.x` — conexión WiFi OK
   - `Sondas DS18B20 detectadas: 2` — ambas sondas de temperatura OK
   - `[C-01] pH: X.XX | Temp: XX.X C | Voltaje: X.XXX V` — lectura OK
   - `HTTP POST OK. Codigo: 200` — envío a n8n OK

### Errores comunes

| Mensaje | Causa probable |
|---|---|
| Sondas DS18B20 detectadas: 0 | Falta resistencia pull-up o cableado |
| pH fuera de rango | Sensor desconectado o sin calibrar |
| No se pudo conectar al WiFi | SSID/password incorrectos |
| Error HTTP: -1 | URL webhook incorrecta o sin internet |

---

## Comportamiento del LED interno (GPIO2)

- **1 parpadeo rápido:** datos enviados correctamente
- **2 parpadeos lentos:** lectura de sensor con advertencia
- **3-5 parpadeos rápidos:** error de WiFi
- **4 parpadeos rápidos:** error en HTTP POST

---

## Intervalo de envío

Por defecto el sketch envía datos cada 5 minutos. Para cambiarlo:

```cpp
#define INTERVALO_ENVIO_MS  300000UL   // 300000 ms = 5 minutos
```

Ejemplos: `60000` = 1 min · `120000` = 2 min · `600000` = 10 min

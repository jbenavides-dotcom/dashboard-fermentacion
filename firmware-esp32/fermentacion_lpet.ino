/*
 * fermentacion_lpet.ino
 * Monitor de fermentacion de cafe - La Palma y El Tucan
 *
 * Hardware:
 *   - ESP32 DevKit V1
 *   - 2x sensor pH PH-4502C (GPIO34 y GPIO35)
 *   - 2x DS18B20 en bus 1-Wire compartido (GPIO4)
 *
 * Autor: Sistema Cerebro #1 - La Palma y El Tucan
 * Fecha: 2026-04-08
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

// =============================================================================
// CREDENCIALES WIFI
// =============================================================================
#define WIFI_SSID     "TU_SSID"
#define WIFI_PASSWORD "TU_PASSWORD"

// =============================================================================
// WEBHOOK n8n
// =============================================================================
#define WEBHOOK_URL "https://tu-n8n-instance.com/webhook/fermentacion"

// =============================================================================
// PINES
// =============================================================================
#define PIN_PH_C01   34
#define PIN_PH_C02   35
#define PIN_ONEWIRE   4
#define PIN_LED_INT   2

// =============================================================================
// CALIBRACION pH
// Medir voltaje con buffer pH 4.0 y pH 7.0, anotar aqui y recompilar
// =============================================================================
#define PH_VOLTAJE_4  3.00    // Voltaje medido con solucion pH 4.0 (Volts)
#define PH_VOLTAJE_7  2.00    // Voltaje medido con solucion pH 7.0 (Volts)

// =============================================================================
// ADC
// =============================================================================
#define ADC_RESOLUCION    4095
#define ADC_VOLTAJE_REF   3.3
#define PH_MUESTRAS       10

// =============================================================================
// INTERVALOS
// =============================================================================
#define INTERVALO_LECTURA_MS   60000UL
#define INTERVALO_ENVIO_MS    300000UL
#define TIMEOUT_WIFI_MS        15000UL
#define TIMEOUT_HTTP_MS         8000

// =============================================================================
// CANECAS
// =============================================================================
#define NOMBRE_C01 "C-01"
#define NOMBRE_C02 "C-02"
#define DESC_C01   "CSP-48 Clarity Select Geisha"
#define DESC_C02   "LPX-500 Lactico Sidra"

// =============================================================================
// OBJETOS GLOBALES
// =============================================================================
OneWire busOneWire(PIN_ONEWIRE);
DallasTemperature sondas(&busOneWire);

float ph_pendiente  = 0.0;
float ph_intercepto = 0.0;

unsigned long ultimaLecturaMs = 0;
unsigned long ultimoEnvioMs   = 0;

struct LecturaCaneca {
  String  id;
  String  descripcion;
  float   ph;
  float   temperatura;
  float   voltaje_raw;
  bool    valida;
};

LecturaCaneca lecturaC01;
LecturaCaneca lecturaC02;
uint8_t erroresWifi = 0;

// =============================================================================
// PROTOTIPOS
// =============================================================================
void   setupWiFi();
void   reconectarWiFi();
float  leerPH(int pin, float &voltaje_out);
bool   leerTemperaturas(float &tempC01, float &tempC02);
void   tomarLecturas();
void   enviarDatos();
void   parpadearLED(int veces, int ms_on, int ms_off);
void   logInfo(String mensaje);
void   logError(String mensaje);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("  Monitor de Fermentacion - LP&ET");
  Serial.println("  Canecas: C-01 (Geisha) y C-02 (Sidra)");
  Serial.println("========================================\n");

  pinMode(PIN_LED_INT, OUTPUT);
  digitalWrite(PIN_LED_INT, LOW);
  analogReadResolution(12);

  // Calcular curva de calibracion pH = pendiente * V + intercepto
  ph_pendiente  = (7.0 - 4.0) / (PH_VOLTAJE_7 - PH_VOLTAJE_4);
  ph_intercepto = 4.0 - ph_pendiente * PH_VOLTAJE_4;

  logInfo("Calibracion pH:");
  Serial.printf("  Voltaje @ pH 4.0 = %.3f V  |  Voltaje @ pH 7.0 = %.3f V\n",
                PH_VOLTAJE_4, PH_VOLTAJE_7);
  Serial.printf("  Pendiente = %.4f  |  Intercepto = %.4f\n",
                ph_pendiente, ph_intercepto);

  sondas.begin();
  int numSondas = sondas.getDeviceCount();
  logInfo("Sondas DS18B20 detectadas: " + String(numSondas));
  if (numSondas < 2) {
    logError("ADVERTENCIA: se esperaban 2 sondas DS18B20. Verificar cableado.");
  }
  sondas.setResolution(12);
  sondas.setWaitForConversion(false);

  lecturaC01 = { NOMBRE_C01, DESC_C01, 0.0, 0.0, 0.0, false };
  lecturaC02 = { NOMBRE_C02, DESC_C02, 0.0, 0.0, 0.0, false };

  setupWiFi();

  // Primera lectura y envio inmediatos
  tomarLecturas();
  enviarDatos();

  logInfo("Setup completado. Lectura: 60s | Envio: 5min");
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  unsigned long ahora = millis();

  if (WiFi.status() != WL_CONNECTED) {
    reconectarWiFi();
  }

  if (ahora - ultimaLecturaMs >= INTERVALO_LECTURA_MS) {
    ultimaLecturaMs = ahora;
    tomarLecturas();
  }

  if (ahora - ultimoEnvioMs >= INTERVALO_ENVIO_MS) {
    ultimoEnvioMs = ahora;
    enviarDatos();
  }
}

// =============================================================================
// setupWiFi: conecta al iniciar, bloquea hasta TIMEOUT_WIFI_MS
// =============================================================================
void setupWiFi() {
  logInfo("Conectando a WiFi: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - inicio > TIMEOUT_WIFI_MS) {
      logError("No se pudo conectar al WiFi en " +
               String(TIMEOUT_WIFI_MS / 1000) + " segundos.");
      parpadearLED(5, 100, 100);
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  erroresWifi = 0;
  logInfo("WiFi conectado. IP: " + WiFi.localIP().toString());
  parpadearLED(1, 200, 0);
}

// =============================================================================
// reconectarWiFi: reintenta si se pierde la conexion
// =============================================================================
void reconectarWiFi() {
  logError("WiFi desconectado. Intentando reconectar...");
  WiFi.disconnect();
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - inicio > TIMEOUT_WIFI_MS) {
      erroresWifi++;
      logError("Reconexion fallida (intento " + String(erroresWifi) + ").");
      parpadearLED(3, 200, 200);
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  erroresWifi = 0;
  logInfo("WiFi reconectado. IP: " + WiFi.localIP().toString());
}

// =============================================================================
// leerPH: filtro de mediana + conversion lineal voltaje->pH
// =============================================================================
float leerPH(int pin, float &voltaje_out) {
  int muestras[PH_MUESTRAS];

  for (int i = 0; i < PH_MUESTRAS; i++) {
    muestras[i] = analogRead(pin);
    delay(5);
  }

  // Ordenar de menor a mayor (burbuja)
  for (int i = 0; i < PH_MUESTRAS - 1; i++) {
    for (int j = 0; j < PH_MUESTRAS - 1 - i; j++) {
      if (muestras[j] > muestras[j + 1]) {
        int tmp     = muestras[j];
        muestras[j] = muestras[j + 1];
        muestras[j + 1] = tmp;
      }
    }
  }

  // Descartar 2 extremos de cada lado, promediar los 6 del centro
  int descarte = 2;
  int validas  = PH_MUESTRAS - 2 * descarte;
  long suma    = 0;
  for (int i = descarte; i < PH_MUESTRAS - descarte; i++) {
    suma += muestras[i];
  }
  float adc_promedio = (float)suma / validas;

  voltaje_out = adc_promedio * ADC_VOLTAJE_REF / ADC_RESOLUCION;

  float ph = ph_pendiente * voltaje_out + ph_intercepto;

  if (ph < 2.5 || ph > 9.0) {
    logError("pH fuera de rango (" + String(ph, 2) +
             ") en pin " + String(pin) + ". Verificar sensor.");
    return -1.0;
  }

  return ph;
}

// =============================================================================
// leerTemperaturas: lee ambas sondas DS18B20
// =============================================================================
bool leerTemperaturas(float &tempC01, float &tempC02) {
  sondas.requestTemperatures();
  delay(800);  // 750 ms necesarios para resolucion 12 bits

  int numSondas = sondas.getDeviceCount();

  if (numSondas == 0) {
    logError("No se encontraron sondas DS18B20. Verificar bus 1-Wire.");
    tempC01 = -127.0;
    tempC02 = -127.0;
    return false;
  }

  tempC01 = sondas.getTempCByIndex(0);

  if (numSondas >= 2) {
    tempC02 = sondas.getTempCByIndex(1);
  } else {
    logError("Solo 1 sonda detectada. Asignando mismo valor a C-02.");
    tempC02 = tempC01;
  }

  bool ok = true;
  if (tempC01 == DEVICE_DISCONNECTED_C || tempC01 < 5.0 || tempC01 > 50.0) {
    logError("Temperatura C-01 invalida: " + String(tempC01, 1) + " C");
    ok = false;
  }
  if (tempC02 == DEVICE_DISCONNECTED_C || tempC02 < 5.0 || tempC02 > 50.0) {
    logError("Temperatura C-02 invalida: " + String(tempC02, 1) + " C");
    ok = false;
  }

  return ok;
}

// =============================================================================
// tomarLecturas: orquesta pH + temperatura, actualiza estructuras globales
// =============================================================================
void tomarLecturas() {
  logInfo("--- Tomando lecturas ---");

  float tempC01 = 0.0, tempC02 = 0.0;
  bool tempOk = leerTemperaturas(tempC01, tempC02);

  float voltajeC01 = 0.0;
  float phC01 = leerPH(PIN_PH_C01, voltajeC01);

  float voltajeC02 = 0.0;
  float phC02 = leerPH(PIN_PH_C02, voltajeC02);

  lecturaC01.ph          = phC01;
  lecturaC01.temperatura = tempC01;
  lecturaC01.voltaje_raw = voltajeC01;
  lecturaC01.valida      = (phC01 > 0.0) && tempOk;

  lecturaC02.ph          = phC02;
  lecturaC02.temperatura = tempC02;
  lecturaC02.voltaje_raw = voltajeC02;
  lecturaC02.valida      = (phC02 > 0.0) && tempOk;

  Serial.printf("  [C-01] pH: %.2f | Temp: %.1f C | Voltaje: %.3f V | %s\n",
                lecturaC01.ph, lecturaC01.temperatura, lecturaC01.voltaje_raw,
                lecturaC01.descripcion.c_str());
  Serial.printf("  [C-02] pH: %.2f | Temp: %.1f C | Voltaje: %.3f V | %s\n",
                lecturaC02.ph, lecturaC02.temperatura, lecturaC02.voltaje_raw,
                lecturaC02.descripcion.c_str());

  if (!lecturaC01.valida || !lecturaC02.valida) {
    parpadearLED(2, 150, 150);
  }
}

// =============================================================================
// enviarDatos: HTTP POST al webhook con JSON de ambas canecas
// =============================================================================
void enviarDatos() {
  if (WiFi.status() != WL_CONNECTED) {
    logError("Sin WiFi. No se puede enviar datos.");
    reconectarWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  logInfo("Enviando datos al webhook...");

  StaticJsonDocument<512> doc;
  doc["timestamp"]   = millis() / 1000;
  doc["dispositivo"] = "ESP32-FERMENTACION-LPET";

  JsonArray lecturas = doc.createNestedArray("lecturas");

  JsonObject c01 = lecturas.createNestedObject();
  c01["caneca_id"]      = lecturaC01.id;
  c01["descripcion"]    = lecturaC01.descripcion;
  c01["ph"]             = serialized(String(lecturaC01.ph, 2));
  c01["temperatura"]    = serialized(String(lecturaC01.temperatura, 1));
  c01["voltaje_raw_ph"] = serialized(String(lecturaC01.voltaje_raw, 3));
  c01["lectura_valida"] = lecturaC01.valida;

  JsonObject c02 = lecturas.createNestedObject();
  c02["caneca_id"]      = lecturaC02.id;
  c02["descripcion"]    = lecturaC02.descripcion;
  c02["ph"]             = serialized(String(lecturaC02.ph, 2));
  c02["temperatura"]    = serialized(String(lecturaC02.temperatura, 1));
  c02["voltaje_raw_ph"] = serialized(String(lecturaC02.voltaje_raw, 3));
  c02["lectura_valida"] = lecturaC02.valida;

  String payload;
  serializeJson(doc, payload);
  Serial.println("  Payload: " + payload);

  HTTPClient http;
  http.begin(WEBHOOK_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(TIMEOUT_HTTP_MS);

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    logInfo("HTTP POST OK. Codigo: " + String(httpCode));
    parpadearLED(1, 100, 0);
  } else {
    logError("Error HTTP: " + http.errorToString(httpCode));
    parpadearLED(4, 100, 100);
  }

  http.end();
}

// =============================================================================
// parpadearLED
// =============================================================================
void parpadearLED(int veces, int ms_on, int ms_off) {
  for (int i = 0; i < veces; i++) {
    digitalWrite(PIN_LED_INT, HIGH);
    if (ms_on  > 0) delay(ms_on);
    digitalWrite(PIN_LED_INT, LOW);
    if (ms_off > 0) delay(ms_off);
  }
}

// =============================================================================
// LOG
// =============================================================================
void logInfo(String mensaje) {
  Serial.println("[INFO]  " + mensaje);
}

void logError(String mensaje) {
  Serial.println("[ERROR] " + mensaje);
}

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ── OLED ───────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── WiFi / MQTT ────────────────────────────────
#define WIFI_SSID     "nombre del wifi"
#define WIFI_PASS     "contraseña del wifi"
#define TB_HOST       "mqtt.thingsboard.cloud" 
#define TB_PORT       1883
#define TB_TOKEN      "tu_token_de_thingsboard"
#define MQTT_HABILITADO false   // ← cambiar a true en hardware real

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long ultimoEnvioSeguro = 0;
#define INTERVALO_SEGURO 600000UL  // 10 minutos

// ── Pines del ESP32 S3 Supermini──────────────────────────────────────
#define PIN_DS18B20      12
#define PIN_LDR          10
#define PIN_LED_VERDE     6
#define PIN_LED_ROJO      7
#define PIN_BUZZER        9
#define BTN_POWER        13
#define BTN_MODE          3
#define BTN_SUBIR         4
#define BTN_BAJAR         5

// ── Modos predeterminados ──────────────────────
struct Perfil {
  const char* nombre;
  float inferior;
  float superior;
};

Perfil perfiles[] = {
  { "Congelados", -15.0, 0.0  },
  { "Frio",        0.0, 10.0  },
  { "Normal",      10.0, 21.0 }
};
const int NUM_PERFILES = 3;

// ── Variables de configuración ─────────────────
float limiteInferior = 0.0;
float limiteSuperior = 5.0;

// ── Estados del sistema ────────────────────────
// APAGADO → ELEGIR_TIPO → ELEGIR_PERFIL
// → MANUAL_INF → MANUAL_SUP → CUENTA_REGRESIVA → MONITOREANDO
enum Estado {
  APAGADO,
  ELEGIR_TIPO,       // Predeterminado o Manual
  ELEGIR_PERFIL,     // Congelados / Frío / Normal
  MANUAL_INF,        // Ajustar límite inferior
  MANUAL_SUP,        // Ajustar límite superior
  CUENTA_REGRESIVA,  // 30 seg antes de activar alertas
  MONITOREANDO
};

Estado estadoActual   = APAGADO;
bool   memoriaFalla   = false;
bool    ultimaApertura  = false;
bool    buzzerEstado    = false;
unsigned long tiempoBuzzer = 0;
#define BUZZER_INTERVALO 500  // ms intermitente
int    seleccion      = 0; // índice navegación

// ── Cuenta regresiva ───────────────────────────
unsigned long tiempoInicioCuenta = 0;
#define ESPERA_MS 40000  // 30 segundos

// ── Anti-rebote ────────────────────────────────
unsigned long lastPress = 0;
#define DEBOUNCE 250

// ── Sensor ────────────────────────────────────
OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);

// ══════════════════════════════════════════════
void apagarLEDs() {
  digitalWrite(PIN_LED_VERDE,    LOW);
  digitalWrite(PIN_LED_ROJO,     LOW);
  digitalWrite(PIN_BUZZER,       LOW);
}

// ── Pantalla ───────────────────────────────────
void mostrarPantalla(float tempActual = 0, int ldrActual = 0) {
  display.clearDisplay();
  display.setTextColor(WHITE);

  switch (estadoActual) {

    case APAGADO:
      display.setTextSize(1);
      display.setCursor(20, 10);
      display.println("-= CRYOTIX =-");
      display.setCursor(10, 28);
      display.println("Sistema apagado");
      display.setCursor(5, 46);
      display.println("[POWER] para iniciar");
      break;

    case ELEGIR_TIPO:
      display.setTextSize(0.5);
      display.setCursor(15, 2);
      display.println("SELECCIONAR MODO");
      display.drawLine(0, 12, 128, 12, WHITE);
      // Opción 0: Predeterminado
      if (seleccion == 0) display.fillRect(0, 18, 128, 18, WHITE);
      display.setTextColor(seleccion == 0 ? BLACK : WHITE);
      display.setCursor(8, 23);
      display.println("> Predeterminado");
      // Opción 1: Manual
      display.setTextColor(WHITE);
      if (seleccion == 1) display.fillRect(0, 38, 128, 18, WHITE);
      display.setTextColor(seleccion == 1 ? BLACK : WHITE);
      display.setCursor(8, 43);
      display.println("> Manual");
      display.setTextColor(WHITE);
      display.setCursor(5, 58);
      break;

    case ELEGIR_PERFIL:
      display.setTextSize(1);
      display.setCursor(20, 2);
      display.println("MODO PREDETERMINADO");
      display.drawLine(0, 12, 128, 12, WHITE);
      for (int i = 0; i < NUM_PERFILES; i++) {
        int yPos = 16 + i * 16;
        if (seleccion == i) display.fillRect(0, yPos, 128, 15, WHITE);
        display.setTextColor(seleccion == i ? BLACK : WHITE);
        display.setCursor(8, yPos + 3);
        display.print(perfiles[i].nombre);
        display.print(": ");
        display.print(perfiles[i].inferior, 0);
        display.print(" a ");
        display.print(perfiles[i].superior, 0);
        display.println("C");
      }
      display.setTextColor(WHITE);
      display.setCursor(5, 58);
      break;

    case MANUAL_INF:
      display.setTextSize(1);
      display.setCursor(25, 2);
      display.println("MODO MANUAL");
      display.drawLine(0, 12, 128, 12, WHITE);
      display.setCursor(5, 18);
      display.println("Limite inferior:");
      display.setTextSize(3);
      display.setCursor(20, 30);
      display.print(limiteInferior, 1);
      display.println("C");
      display.setTextSize(1);
      display.setCursor(5, 58);
      break;

    case MANUAL_SUP:
      display.setTextSize(1);
      display.setCursor(25, 2);
      display.println("MODO MANUAL");
      display.drawLine(0, 12, 128, 12, WHITE);
      display.setCursor(5, 18);
      display.println("Limite superior:");
      display.setTextSize(3);
      display.setCursor(20, 30);
      display.print(limiteSuperior, 1);
      display.println("C");
      display.setTextSize(1);
      display.setCursor(5, 58);
      break;

    case CUENTA_REGRESIVA: {
      int segundosRestantes = (ESPERA_MS - (millis() - tiempoInicioCuenta)) / 1000;
      if (segundosRestantes < 0) segundosRestantes = 0;
      display.setTextSize(1);
      display.setCursor(15, 2);
      display.println("INICIANDO EN...");
      display.drawLine(0, 12, 128, 12, WHITE);
      display.setTextSize(4);
      display.setCursor(40, 20);
      display.println(segundosRestantes);
      display.setTextSize(1);
      display.setCursor(5, 56);
      display.print("Inf:");
      display.print(limiteInferior, 1);
      display.print("  Sup:");
      display.println(limiteSuperior, 1);
      break;
    }

    case MONITOREANDO: {
      String estadoStr;
      if (memoriaFalla)                        estadoStr = "!! FALLA !!";
      else if (tempActual > limiteSuperior)    estadoStr = "CRITICO";
      else if (tempActual < limiteInferior)    estadoStr = "CRITICO";
      else if (tempActual > limiteSuperior - 1) estadoStr = "ADVERTENCIA";
      else                                     estadoStr = "SEGURO";

      display.setTextSize(1);
      display.setCursor(30, 2);
      display.println("MONITOREANDO");
      display.drawLine(0, 12, 128, 12, WHITE);
      display.setCursor(5, 15);
      display.println("Temperatura:");
      display.setTextSize(2);
      display.setCursor(15, 25);
      display.print(tempActual, 1);
      display.println(" C");
      display.setTextSize(1);
      display.setCursor(5, 45);
      display.print("Rango: ");
      display.print(limiteInferior, 1);
      display.print(" - ");
      display.print(limiteSuperior, 1);
      display.println("C");
      display.setCursor(5, 55);
      display.println(estadoStr);
      break;
    }
  }
  display.display();
}

// ── Botones ────────────────────────────────────
void leerBotones() {
  if (millis() - lastPress < DEBOUNCE) return;
   display.ssd1306_command(SSD1306_DISPLAYON);
  // ── POWER: siempre disponible ──
  if (digitalRead(BTN_POWER) == LOW) {
    lastPress = millis();
    if (estadoActual != APAGADO) {
      // Apagar
      estadoActual  = APAGADO;
      memoriaFalla  = false;
      seleccion     = 0;
      apagarLEDs();
    } else {
      // Encender
      estadoActual = ELEGIR_TIPO;
      seleccion    = 0;
    }
    mostrarPantalla();
    return;
  }

  // ── Navegación según estado ──
  switch (estadoActual) {

    case ELEGIR_TIPO:
      if (digitalRead(BTN_SUBIR) == LOW || digitalRead(BTN_BAJAR) == LOW) {
        seleccion = (seleccion == 0) ? 1 : 0;
        lastPress = millis();
        mostrarPantalla();
      } else if (digitalRead(BTN_MODE) == LOW) {
        lastPress = millis();
        if (seleccion == 0) {
          estadoActual = ELEGIR_PERFIL;
          seleccion    = 0;
        } else {
          estadoActual    = MANUAL_INF;
          limiteInferior  = 0.0;
          limiteSuperior  = 5.0;
        }
        mostrarPantalla();
      }
      break;

    case ELEGIR_PERFIL:
      if (digitalRead(BTN_SUBIR) == LOW) {
        seleccion = (seleccion + 1) % NUM_PERFILES;
        lastPress = millis();
        mostrarPantalla();
      } else if (digitalRead(BTN_BAJAR) == LOW) {
        seleccion = (seleccion - 1 + NUM_PERFILES) % NUM_PERFILES;
        lastPress = millis();
        mostrarPantalla();
      } else if (digitalRead(BTN_MODE) == LOW) {
        lastPress        = millis();
        limiteInferior   = perfiles[seleccion].inferior;
        limiteSuperior   = perfiles[seleccion].superior;
        estadoActual     = CUENTA_REGRESIVA;
        tiempoInicioCuenta = millis();
        mostrarPantalla();
      }
      break;

    case MANUAL_INF:
      if (digitalRead(BTN_SUBIR) == LOW) {
        limiteInferior += 0.5;
        lastPress = millis();
        mostrarPantalla();
      } else if (digitalRead(BTN_BAJAR) == LOW) {
        limiteInferior -= 0.5;
        lastPress = millis();
        mostrarPantalla();
      } else if (digitalRead(BTN_MODE) == LOW) {
        lastPress        = millis();
        // Superior debe ser al menos 1°C mayor que inferior
        limiteSuperior   = limiteInferior + 1.0;
        estadoActual     = MANUAL_SUP;
        mostrarPantalla();
      }
      break;

    case MANUAL_SUP:
      if (digitalRead(BTN_SUBIR) == LOW) {
        limiteSuperior += 0.5;
        lastPress = millis();
        mostrarPantalla();
      } else if (digitalRead(BTN_BAJAR) == LOW) {
        if (limiteSuperior > limiteInferior + 0.5) limiteSuperior -= 0.5;
        lastPress = millis();
        mostrarPantalla();
      } else if (digitalRead(BTN_MODE) == LOW) {
        lastPress          = millis();
        estadoActual       = CUENTA_REGRESIVA;
        tiempoInicioCuenta = millis();
        mostrarPantalla();
      }
      break;

    default:
      break;
  }
}
void conectarMQTT() {
  if (!MQTT_HABILITADO) return;
  mqtt.setServer(TB_HOST, TB_PORT);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando WiFi");
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 30) {
    delay(500); Serial.print("."); intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK - IP: " + WiFi.localIP().toString());
    delay(500);
    if (mqtt.connect("Cryotix", TB_TOKEN, NULL)) {
      Serial.println("MQTT OK");
    } else {
      Serial.print("MQTT FALLO, codigo: ");
      Serial.println(mqtt.state());
    }
  } else {
    Serial.println("\nWiFi FALLO - modo offline");
  }
}

void enviarMQTT(float temp, bool apertura) {
  if (!MQTT_HABILITADO) return;
  
  int reintentos = 0;
  while (!mqtt.connected() && reintentos < 3) {
    Serial.print("Reconectando MQTT...");
    if (mqtt.connect("Cryotix", TB_TOKEN, "")) {
      Serial.println("OK");
    } else {
      Serial.print("FALLO, codigo: ");
      Serial.println(mqtt.state());
      reintentos++;
      delay(1000);
    }
  }
  
  if (!mqtt.connected()) return;
  int luz = digitalRead(PIN_LDR);
  bool contenedorAbierto = (luz == 0);

  String payload = "{";
  payload += "\"temperature\":" + String(temp, 1) + ",";
  payload += "\"state\":\"" + String(memoriaFalla ? "CRITICO" : "SEGURO") + "\",";
  payload += "\"ldr\":" + String(luz) + ",";
  payload += "\"failureMemory\":" + String(memoriaFalla ? "true" : "false");
  payload += "}";

  mqtt.publish("v1/devices/me/telemetry", payload.c_str());
  Serial.println("MQTT enviado: " + payload);
}

// ══════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  sensors.begin();

  pinMode(PIN_LED_VERDE,    OUTPUT);
  pinMode(PIN_LED_ROJO,     OUTPUT);
  pinMode(PIN_BUZZER,       OUTPUT);
  pinMode(BTN_POWER, INPUT_PULLUP);
  pinMode(BTN_MODE,  INPUT_PULLUP);
  pinMode(BTN_SUBIR, INPUT_PULLUP);
  pinMode(BTN_BAJAR, INPUT_PULLUP);

  Wire.begin(1, 2);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  mostrarPantalla();
  Serial.println("Cryotix iniciado.");
  conectarMQTT();
}

// ══════════════════════════════════════════════
void loop() {
  leerBotones();

  // ── Cuenta regresiva ──────────────────────
  if (estadoActual == CUENTA_REGRESIVA) {
    mostrarPantalla();
    if (millis() - tiempoInicioCuenta >= ESPERA_MS) {
      estadoActual = MONITOREANDO;
      memoriaFalla = false;
    }
    delay(500);
    return;
  }

  if (estadoActual != MONITOREANDO) return;

  // ── Leer sensores ─────────────────────────
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  int   luz  = digitalRead(PIN_LDR);
  bool  contenedorAbierto = (luz == 0);

// ── Lógica de alertas ─────────────────────
  apagarLEDs();

  bool fueradeRango = (temp > limiteSuperior || temp < limiteInferior);
  bool aperturaDetectada = (luz == 0);

  if (fueradeRango) memoriaFalla = true;

  if (memoriaFalla) {
    // Buzzer intermitente
    if (millis() - tiempoBuzzer >= BUZZER_INTERVALO) {
      buzzerEstado = !buzzerEstado;
      tiempoBuzzer = millis();
    }
    digitalWrite(PIN_LED_ROJO,  HIGH);
    digitalWrite(PIN_BUZZER,    buzzerEstado ? HIGH : LOW);

    // Envío inmediato MQTT en crítico
    enviarMQTT(temp, aperturaDetectada);
    Serial.println("ESTADO: CRITICO (memoria de falla)");

  } else {
    digitalWrite(PIN_LED_VERDE, HIGH);
    Serial.println("ESTADO: SEGURO");

    // Envío periódico cada 10 min
    if (millis() - ultimoEnvioSeguro >= INTERVALO_SEGURO) {
      enviarMQTT(temp, aperturaDetectada);
      ultimoEnvioSeguro = millis();
    }
  }

  // ── Apertura del contenedor ────────────────
  if (aperturaDetectada && !ultimaApertura) {
    Serial.println("⚠ Contenedor ABIERTO");
    enviarMQTT(temp, true);  // envío inmediato
  }
  ultimaApertura = aperturaDetectada;

  // ── OLED: apagar si está dentro del cooler ─
 mostrarPantalla(temp, luz);

  Serial.print("Temp: "); Serial.print(temp);
  Serial.print(" C  |  Luz: "); Serial.print(luz);
  Serial.print("  |  Rango: "); Serial.print(limiteInferior);
  Serial.print(" - "); Serial.println(limiteSuperior);
  Serial.println("─────────────────────");

  mqtt.loop();
  delay(2000);
}

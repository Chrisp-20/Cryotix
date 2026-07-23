# Cryotix — "Cada grado importa" 🧊

Dispositivo IoT de monitoreo de temperatura para cadenas de frío, pensado para pequeños negocios de alimentos (PYMEs). Detecta cuándo la temperatura sale del rango seguro y avisa en tiempo real.

Proyecto desarrollado para el curso **CIT2205 – Proyecto en TICs I**, Universidad Diego Portales.

## Cómo funciona

El sistema recorre distintos estados según los botones físicos:

```
APAGADO → ELEGIR_TIPO → ELEGIR_PERFIL / MANUAL → CUENTA_REGRESIVA → MONITOREANDO
```

- **Predeterminado**: perfiles listos para uso (Congelados, Frío, Normal), cada uno con su rango de temperatura.
- **Manual**: el usuario define sus propios límites inferior y superior.
- **Monitoreando**: lee la temperatura cada 2 segundos y evalúa el estado (SEGURO / ADVERTENCIA / CRÍTICO). Si detecta una falla, queda en "memoria de falla" hasta que se reinicia el sistema — así no se pierde el aviso aunque la temperatura vuelva a la normalidad.
- La apertura del contenedor se detecta con un sensor de luz (LDR).

## Hardware

| Componente | Función |
|---|---|
| ESP32-S3 Supermini | Microcontrolador principal |
| DS18B20 | Sensor de temperatura |
| SSD1306 (OLED 128x64) | Pantalla de estado |
| Módulo LDR | Detección de apertura del contenedor |
| LEDs verde/rojo + buzzer | Alertas locales |
| 4 botones (Power, Mode, Subir, Bajar) | Navegación del sistema |

La carcasa fue diseñada en Fusion 360 e impresa en PLA (archivos 3MF no incluidos en este repo).

## Conectividad

Los datos se envían por MQTT a un dashboard en **ThingsBoard**, donde se pueden ver las tendencias de temperatura y configurar alertas al celular.

## Configuración

Antes de subir el código a la placa, reemplaza estas líneas con tus propios datos:

```cpp
#define WIFI_SSID     "nombre del wifi"
#define WIFI_PASS     "contraseña del wifi"
#define TB_TOKEN      "tu_token_de_thingsboard"
```

El token de ThingsBoard se obtiene desde el panel del dispositivo en tu cuenta (Device → Details → Copy device credentials).

Por defecto `MQTT_HABILITADO` está en `false` para poder probar el sistema sin conexión; cámbialo a `true` cuando lo uses con hardware real conectado a WiFi.

## Librerías necesarias (Arduino IDE)

- OneWire
- DallasTemperature
- Adafruit GFX
- Adafruit SSD1306
- WiFi (ESP32 core)
- PubSubClient

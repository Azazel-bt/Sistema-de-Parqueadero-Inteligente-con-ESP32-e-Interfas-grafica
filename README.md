# 🎯 Parqueadero Inteligente IoT con ESP32 y Control en Tiempo Real

Un sistema IoT integral basado en el microcontrolador **ESP32** para la automatización, gestión y monitoreo en tiempo real de plazas de estacionamiento y control de acceso mediante barreras vehiculares servomotorizadas.

<img width="1600" height="900" alt="WhatsApp Image 2026-10-20 at 12 59 29 PM" src="https://github.com/user-attachments/assets/d8446898-ed79-48bf-b938-b3998aad9707" />


---

## 🚀 Características Principales

* **Monitoreo en Tiempo Real:** Detección de presencia en 4 plazas de estacionamiento independientes mediante sensores de efecto Hall analógicos.


* **Control Automático y Manual de Barreras:** Servomotores gestionados dinámicamente para entrada y salida con temporizadores de autocierre y algoritmos de bloqueo (*lockout*) anti-conflicto manual/automático.


* **Interfaz Web Interactiva (Dashboard):** Servidor HTTP embebido en el ESP32 configurado en modo Punto de Acceso (Access Point Wi-Fi), permitiendo monitorear el tiempo de ocupación por plaza y accionar las barreras manualmente desde cualquier navegador web.


* **Filtrado Dinámico e Histéresis:** Implementación de filtro de promedio móvil para señales analógicas y algoritmo de histéresis con calibración base continua para eliminar falsos positivos debido a ruido electromagnético o derivas térmicas.


* **Indicadores Locales y Web:** Salidas digitales para luces LED testigo en cada plaza y sincronización mediante peticiones JSON/AJAX periódicas (cada 1s) sin recargar la página.



---

## 🧮 Arquitectura de Software y Control

### 1. Calibración y Filtrado de Sensores Hall (49E)

Para evitar lecturas erróneas en los pines ADC del ESP32, las lecturas analógicas se procesan mediante un filtro de promedio móvil de $N$ muestras y recalibración adaptativa en reposo:

$$V_{filtrado} = \frac{1}{N} \sum_{i=1}^{N} V_{ADC}[i]$$

$$\text{Desviación} = \vert{}V_{filtrado} - V_{base}\vert{}$$

### 2. Algoritmo de Histéresis
Para la detección robusta de vehículos sobre las plazas, el cambio de estado de detección utiliza dos umbrales diferenciados:

$$\text{Estado}_{k} = \begin{cases} \text{OCUPADO}, & \text{si } \text{Desviacion} > \text{UMBRAL DESVIACION} \\ \text{LIBRE}, & \text{si } \text{Desviacion} < \text{UMBRAL HISTERESIS} \end{cases}$$

---

## 💻 Requisitos e Instalación

### Requisitos de Hardware

* **Microcontrolador:** ESP32 Development Board (NodeMCU ESP32 / WROOM-32).


* **Actuadores:** 2x Servomotores Micro SG90 (Entrada y Salida).


* **Sensores:** 6x Sensores de efecto Hall analógicos (49E) (4 plazas + Entrada + Salida).


* **Indicadores:** 4x LEDs con sus respectivas resistencias limitadoras.



### Dependencias de Software / Librerías (Arduino IDE)

* `WiFi.h` (Nativa ESP32)


* `WebServer.h` (Nativa ESP32)


* `ESP32Servo.h` (Biblioteca de control PWM para servomotores en ESP32)



### Instrucciones de Ejecución

1. Clona este repositorio:
```bash
git clone https://github.com/Azazel-bt/Parqueadero-Inteligente-ESP32.git

```


2. Abre el archivo de firmware `.ino` en la IDE de Arduino.
3. Configura la placa **ESP32 Dev Module** y selecciona el puerto COM adecuado.
4. Compila y carga el código a la placa ESP32.


5. Conéctate a la red Wi-Fi emitida por la placa:
* **SSID:** `Parqueadero_ESP32`

* **Password:** `12345678`



6. Abre tu navegador e ingresa a la dirección IP por defecto: `[http://192.168.4.1](http://192.168.4.1)`.



---

## 📁 Estructura del Repositorio

```text
├── Firmware/
│   └── Parqueadero_ESP32.ino    # Código principal con WebServer, sensores y servos
├── Hardware/
│   └── Schematics.pdf           # Esquemático de conexiones y diagrama de pines
├── Hardware_BOM.md              # Lista de materiales (Bill of Materials)
├── README.md                    # Documentación principal del proyecto
└── LICENSE                      # Licencia del proyecto

```

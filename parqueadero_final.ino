#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// --- Prototipos ---
void handleRoot();
void handleStatus();
void abrirPuerta(int tipo);
void gestionServosAuto();
void actualizarSensoresYTiempos();
int leerHallFiltrado(int pin, int muestras);

// --- WiFi ---
const char* ssid = "Parqueadero_ESP32";
const char* password = "12345678";
WebServer server(80);

// --- Pines ---
const int PIN_HALL[4]    = {34, 35, 32, 33}; // Hall analógicos plazas
const int PIN_HALL_ENT   = 36;               // Hall analógico ENTRADA
const int PIN_HALL_SAL   = 39;               // Hall analógico SALIDA
const int PIN_LED[4]     = {4, 5, 21, 22};   // LEDs testigos por plaza
const int PIN_SERVO_IN   = 18;
const int PIN_SERVO_OUT  = 19;

// --- Servos ---
Servo servoEntrada;
Servo servoSalida;

// --- Variables de Estado ---
bool plazaOcupada[4] = {false, false, false, false};
unsigned long tiempoInicio[4] = {0, 0, 0, 0};
unsigned long tiempoAcumulado[4] = {0, 0, 0, 0};

unsigned long tiempoServoEntrada = 0;
bool servoEntradaAbierto = false;

unsigned long tiempoServoSalida = 0;
bool servoSalidaAbierto = false;

// --- Hall 49E ---
const int UMBRAL_DESVIACION = 250;        // umbral para ACTIVAR
const int UMBRAL_HISTERESIS = 100;        // umbral para DESACTIVAR (más bajo)
int valorBaseHall[4]    = {0, 0, 0, 0};
int valorBaseHallEnt    = 0;
int valorBaseHallSal    = 0;
unsigned long ultimoCambioHall[4] = {0, 0, 0, 0};
bool estadoHallAnterior[4] = {false, false, false, false};
bool estadoEntradaAnterior = false;
bool estadoSalidaAnterior  = false;

const unsigned long DEBOUNCE_MS = 300;
const unsigned long TIEMPO_APERTURA_MS = 6000;

// --- Lockout automático (evita que el sensor pelee con el control manual) ---
unsigned long bloqueoAutoEnt = 0;
unsigned long bloqueoAutoSal = 0;
const unsigned long BLOQUEO_AUTO_MS = 8000;  // 8 segundos de bloqueo tras comando manual
unsigned long ultimoDebug = 0;

// --- Ángulos de servo ---
const int ANG_CERRADO_IN  = 0;
const int ANG_ABIERTO_IN  = 90;
const int ANG_CERRADO_SAL = 180;   // Invertido para el servo de salida
const int ANG_ABIERTO_SAL = 90;    // Invertido para el servo de salida

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) pinMode(PIN_HALL[i], INPUT);
  pinMode(PIN_HALL_ENT, INPUT);
  pinMode(PIN_HALL_SAL, INPUT);

  for (int i = 0; i < 4; i++) {
    pinMode(PIN_LED[i], OUTPUT);
    digitalWrite(PIN_LED[i], LOW);
  }

  // --- Calibración ---
  Serial.println("Calibrando Hall de plazas (vacias)...");
  delay(500);
  for (int i = 0; i < 4; i++) {
    long suma = 0;
    for (int j = 0; j < 20; j++) {
      suma += analogRead(PIN_HALL[i]);
      delay(10);
    }
    valorBaseHall[i] = suma / 20;
    Serial.print("Sensor plaza "); Serial.print(i + 1);
    Serial.print(" base: ");       Serial.println(valorBaseHall[i]);
  }

  Serial.println("Calibrando Hall de entrada/salida (sin vehiculo)...");
  long sumaE = 0, sumaS = 0;
  for (int j = 0; j < 20; j++) {
    sumaE += analogRead(PIN_HALL_ENT);
    sumaS += analogRead(PIN_HALL_SAL);
    delay(10);
  }
  valorBaseHallEnt = sumaE / 20;
  valorBaseHallSal = sumaS / 20;
  Serial.print("Base ENTRADA (pin 36): "); Serial.println(valorBaseHallEnt);
  Serial.print("Base SALIDA  (pin 39): "); Serial.println(valorBaseHallSal);

  if (valorBaseHallEnt < 200 || valorBaseHallEnt > 3900) {
    Serial.println("ADVERTENCIA: base de ENTRADA esta fuera de rango. Revisa el sensor!");
  }
  if (valorBaseHallSal < 200 || valorBaseHallSal > 3900) {
    Serial.println("ADVERTENCIA: base de SALIDA esta fuera de rango. Revisa el sensor!");
  }

  // --- Servos ---
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoEntrada.setPeriodHertz(50);
  servoSalida.setPeriodHertz(50);
  servoEntrada.attach(PIN_SERVO_IN, 500, 2400);
  servoSalida.attach(PIN_SERVO_OUT, 500, 2400);
  servoEntrada.write(ANG_CERRADO_IN);
  servoSalida.write(ANG_CERRADO_SAL);

  // --- WiFi ---
  WiFi.softAP(ssid, password);
  Serial.print("Punto de acceso activo. IP: ");
  Serial.println(WiFi.softAPIP());

  // --- Rutas ---
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);

  server.on("/abrirEntrada", HTTP_GET, []() {
    bloqueoAutoEnt = millis();
    abrirPuerta(1);
    server.send(200, "text/plain", "OK");
  });
  server.on("/abrirSalida", HTTP_GET, []() {
    bloqueoAutoSal = millis();
    abrirPuerta(2);
    server.send(200, "text/plain", "OK");
  });
  server.on("/cerrarEntrada", HTTP_GET, []() {
    bloqueoAutoEnt = millis();
    servoEntrada.write(ANG_CERRADO_IN);
    servoEntradaAbierto = false;
    Serial.println("<<< BARRERA ENTRADA cerrada (manual)");
    server.send(200, "text/plain", "OK");
  });
  server.on("/cerrarSalida", HTTP_GET, []() {
    bloqueoAutoSal = millis();
    servoSalida.write(ANG_CERRADO_SAL);
    servoSalidaAbierto = false;
    Serial.println("<<< BARRERA SALIDA cerrada (manual)");
    server.send(200, "text/plain", "OK");
  });

  server.begin();
  Serial.println("Servidor web iniciado.");
}

void loop() {
  server.handleClient();
  actualizarSensoresYTiempos();
  gestionServosAuto();
}

// --- Lee un pin analógico con promedio móvil para reducir ruido ---
int leerHallFiltrado(int pin, int muestras) {
  long suma = 0;
  for (int i = 0; i < muestras; i++) {
    suma += analogRead(pin);
    delayMicroseconds(200);
  }
  return suma / muestras;
}

void abrirPuerta(int tipo) {
  if (tipo == 1 && !servoEntradaAbierto) {
    servoEntrada.write(ANG_ABIERTO_IN);
    servoEntradaAbierto = true;
    tiempoServoEntrada = millis();
    Serial.println(">>> BARRERA ENTRADA abierta");
  } else if (tipo == 2 && !servoSalidaAbierto) {
    servoSalida.write(ANG_ABIERTO_SAL);
    servoSalidaAbierto = true;
    tiempoServoSalida = millis();
    Serial.println(">>> BARRERA SALIDA abierta");
  }
}

void gestionServosAuto() {
  int vE = leerHallFiltrado(PIN_HALL_ENT, 8);
  int vS = leerHallFiltrado(PIN_HALL_SAL, 8);

  int devE = abs(vE - valorBaseHallEnt);
  int devS = abs(vS - valorBaseHallSal);

  bool detectaEntrada = (devE > UMBRAL_DESVIACION);
  bool detectaSalida  = (devS > UMBRAL_DESVIACION);

  // Validar que el base no esté "railado" (pin desconectado o dañado)
  bool baseEntValida = (valorBaseHallEnt > 200 && valorBaseHallEnt < 3900);
  bool baseSalValida = (valorBaseHallSal > 200 && valorBaseHallSal < 3900);

  // Auto-apertura con validación de base y lockout tras comando manual
  if (detectaEntrada && !servoEntradaAbierto && baseEntValida &&
      (millis() - bloqueoAutoEnt > BLOQUEO_AUTO_MS)) {
    abrirPuerta(1);
  }
  if (detectaSalida && !servoSalidaAbierto && baseSalValida &&
      (millis() - bloqueoAutoSal > BLOQUEO_AUTO_MS)) {
    abrirPuerta(2);
  }

  // Auto-cierre por tiempo
  if (servoEntradaAbierto && (millis() - tiempoServoEntrada >= TIEMPO_APERTURA_MS)) {
    servoEntrada.write(ANG_CERRADO_IN);
    servoEntradaAbierto = false;
    Serial.println("<<< BARRERA ENTRADA cerrada (auto)");
  }
  if (servoSalidaAbierto && (millis() - tiempoServoSalida >= TIEMPO_APERTURA_MS)) {
    servoSalida.write(ANG_CERRADO_SAL);
    servoSalidaAbierto = false;
    Serial.println("<<< BARRERA SALIDA cerrada (auto)");
  }
}

void actualizarSensoresYTiempos() {
  unsigned long ahora = millis();

  // ---- Plazas 1 a 4 ----
  for (int i = 0; i < 4; i++) {
    int valorActual = leerHallFiltrado(PIN_HALL[i], 8);
    int desviacion = abs(valorActual - valorBaseHall[i]);

    // Histéresis: umbral diferente según estado actual
    bool lecturaActual = estadoHallAnterior[i]
                          ? (desviacion > UMBRAL_HISTERESIS)
                          : (desviacion > UMBRAL_DESVIACION);

    if (lecturaActual != estadoHallAnterior[i]) {
      if (ahora - ultimoCambioHall[i] > DEBOUNCE_MS) {
        estadoHallAnterior[i] = lecturaActual;
        ultimoCambioHall[i] = ahora;

        if (lecturaActual && !plazaOcupada[i]) {
          plazaOcupada[i] = true;
          tiempoInicio[i] = ahora;
          Serial.print(">>> Plaza "); Serial.print(i + 1);
          Serial.println(" OCUPADA");
        } else if (!lecturaActual && plazaOcupada[i]) {
          plazaOcupada[i] = false;
          tiempoAcumulado[i] = 0;
          Serial.print("<<< Plaza "); Serial.print(i + 1);
          Serial.println(" LIBRE");
        }
      }
    }

    if (plazaOcupada[i]) {
      tiempoAcumulado[i] = (ahora - tiempoInicio[i]) / 1000UL;
    }

    digitalWrite(PIN_LED[i], plazaOcupada[i] ? HIGH : LOW);
  }

  // ---- Entrada / Salida ----
  int vE = leerHallFiltrado(PIN_HALL_ENT, 8);
  int vS = leerHallFiltrado(PIN_HALL_SAL, 8);

  int devE = abs(vE - valorBaseHallEnt);
  int devS = abs(vS - valorBaseHallSal);

  bool detectaEntrada = (devE > UMBRAL_DESVIACION);
  bool detectaSalida  = (devS > UMBRAL_DESVIACION);

  // Recalibración suave del base (solo cuando NO hay detección y servo cerrado)
  if (!detectaEntrada && !servoEntradaAbierto) {
    valorBaseHallEnt = (valorBaseHallEnt * 9 + vE) / 10;
  }
  if (!detectaSalida && !servoSalidaAbierto) {
    valorBaseHallSal = (valorBaseHallSal * 9 + vS) / 10;
  }

  // Cambios de estado en entrada
  if (detectaEntrada != estadoEntradaAnterior) {
    estadoEntradaAnterior = detectaEntrada;
    Serial.print("[HALL ENTRADA pin 36] ");
    Serial.print(detectaEntrada ? "VEHICULO DETECTADO" : "libre");
    Serial.print("  (v="); Serial.print(vE);
    Serial.print(", base="); Serial.print(valorBaseHallEnt);
    Serial.print(", dev="); Serial.print(devE);
    Serial.println(")");
  }

  // Cambios de estado en salida
  if (detectaSalida != estadoSalidaAnterior) {
    estadoSalidaAnterior = detectaSalida;
    Serial.print("[HALL SALIDA  pin 39] ");
    Serial.print(detectaSalida ? "VEHICULO DETECTADO" : "libre");
    Serial.print("  (v="); Serial.print(vS);
    Serial.print(", base="); Serial.print(valorBaseHallSal);
    Serial.print(", dev="); Serial.print(devS);
    Serial.println(")");
  }

  // Debug cada 2 s
  if (ahora - ultimoDebug > 2000) {
    ultimoDebug = ahora;
    Serial.print("[DBG] PIN36 raw="); Serial.print(vE);
    Serial.print(" base="); Serial.print(valorBaseHallEnt);
    Serial.print(" dev=");  Serial.print(devE);
    Serial.print("  |  PIN39 raw="); Serial.print(vS);
    Serial.print(" base="); Serial.print(valorBaseHallSal);
    Serial.print(" dev=");  Serial.println(devS);
  }
}

void handleStatus() {
  String json = "{";
  json += "\"p\":[" + String(plazaOcupada[0]) + "," + String(plazaOcupada[1]) + "," +
          String(plazaOcupada[2]) + "," + String(plazaOcupada[3]) + "],";
  json += "\"t\":[" + String(tiempoAcumulado[0]) + "," + String(tiempoAcumulado[1]) + "," +
          String(tiempoAcumulado[2]) + "," + String(tiempoAcumulado[3]) + "],";
  json += "\"e\":" + String(servoEntradaAbierto) + ",";
  json += "\"s\":" + String(servoSalidaAbierto);
  json += "}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Parqueadero ESP32</title>
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Arial, sans-serif;
      background: #0a1220;
      color: #eee;
      text-align: center;
      margin: 0;
      padding: 20px;
    }
    h1 { color: #ff8c00; letter-spacing: 1px; margin-bottom: 5px; }
    .subtitle { color: #6fa8dc; margin-bottom: 25px; font-size: 0.9em; }

    .lot {
      background: #101a2b;
      border: 3px solid #1a3a5c;
      border-radius: 14px;
      max-width: 620px;
      margin: 0 auto;
      padding: 25px 15px 15px 15px;
      box-shadow: inset 0 0 25px rgba(0,0,0,0.6);
      position: relative;
    }
    .spaces {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 0;
      position: relative;
    }
    .space {
      position: relative;
      padding: 30px 5px 15px 5px;
      border-left: 4px dashed #ff8c00;
      border-right: 4px dashed #ff8c00;
      margin: 0 4px;
      border-radius: 4px;
      transition: background 0.3s;
    }
    .space:first-child { border-left: none; margin-left: 0; }
    .space:last-child { border-right: none; margin-right: 0; }
    .space.libre { background: rgba(30,100,180,0.25); }
    .space.ocupada { background: rgba(255,140,0,0.30); }

    .num { font-size: 2em; color: #4a7bb5; font-weight: bold; }
    .estado {
      margin-top: 8px;
      font-weight: bold;
      font-size: 0.95em;
      padding: 4px 8px;
      border-radius: 6px;
      display: inline-block;
    }
    .space.libre .estado { background: #1565c0; color: #fff; }
    .space.ocupada .estado { background: #e65100; color: #fff; }
    .tiempo { margin-top: 6px; font-family: monospace; color: #9fc5e8; font-size: 0.9em; }

    .caseta {
      margin: 25px auto 0;
      max-width: 220px;
      background: linear-gradient(#1565c0, #0d47a1);
      border-radius: 10px 10px 4px 4px;
      padding: 10px;
      box-shadow: 0 4px 10px rgba(0,0,0,0.5);
      position: relative;
    }
    .caseta::before {
      content: "";
      position: absolute;
      top: -14px; left: 10%;
      width: 80%; height: 20px;
      background: #ff8c00;
      border-radius: 8px 8px 0 0;
      transform: perspective(50px) rotateX(10deg);
    }
    .caseta-label { color: #fff; font-weight: bold; font-size: 0.85em; }

    .barreras {
      display: flex;
      justify-content: center;
      gap: 30px;
      margin-top: 20px;
      flex-wrap: wrap;
    }
    .barrera {
      background: #14213d;
      border-radius: 10px;
      padding: 14px 18px;
      min-width: 150px;
      border: 1px solid #1a3a5c;
    }
    .barrera h3 { margin: 0 0 8px; font-size: 0.95em; color: #ff8c00; }
    .barra-visual {
      width: 90px; height: 8px; background: #e65100;
      margin: 8px auto; border-radius: 4px;
      transition: transform 0.4s ease;
      transform-origin: left center;
    }
    .barra-visual.abierta { transform: rotate(-60deg); background:#1976d2; }
    .estado-txt { font-size: 0.85em; color: #9fc5e8; margin-bottom: 8px; }
    .btn-row { display: flex; gap: 6px; justify-content: center; }
    button {
      border: none; color: #fff; padding: 8px 14px;
      font-size: 13px; border-radius: 6px; cursor: pointer;
      transition: 0.15s; font-weight: bold;
    }
    .btn-abrir { background: #1976d2; }
    .btn-abrir:hover { background: #125ea8; }
    .btn-cerrar { background: #ff8c00; }
    .btn-cerrar:hover { background: #e07800; }
    button:active { transform: scale(0.95); }

    .footer { margin-top: 25px; font-size: 0.75em; color: #4a7bb5; }
  </style>
</head>
<body>
  <h1>PARQUEADERO ESP32</h1>
  <div class="subtitle">Monitoreo en tiempo real &bull; Control de barreras</div>

  <div class="lot">
    <div class="spaces">
      <div id="p0" class="space libre">
        <div class="num">1</div>
        <div class="estado" id="e0">LIBRE</div>
        <div class="tiempo" id="t0">00:00</div>
      </div>
      <div id="p1" class="space libre">
        <div class="num">2</div>
        <div class="estado" id="e1">LIBRE</div>
        <div class="tiempo" id="t1">00:00</div>
      </div>
      <div id="p2" class="space libre">
        <div class="num">3</div>
        <div class="estado" id="e2">LIBRE</div>
        <div class="tiempo" id="t2">00:00</div>
      </div>
      <div id="p3" class="space libre">
        <div class="num">4</div>
        <div class="estado" id="e3">LIBRE</div>
        <div class="tiempo" id="t3">00:00</div>
      </div>
    </div>

    <div class="caseta">
      <div class="caseta-label">CASETA DE CONTROL</div>
    </div>
  </div>

  <div class="barreras">
    <div class="barrera">
      <h3>BARRERA ENTRADA</h3>
      <div class="barra-visual" id="barra-e"></div>
      <div class="estado-txt" id="st-e">Cerrada</div>
      <div class="btn-row">
        <button class="btn-abrir" onclick="accion('abrirEntrada')">Abrir</button>
        <button class="btn-cerrar" onclick="accion('cerrarEntrada')">Cerrar</button>
      </div>
    </div>
    <div class="barrera">
      <h3>BARRERA SALIDA</h3>
      <div class="barra-visual" id="barra-s"></div>
      <div class="estado-txt" id="st-s">Cerrada</div>
      <div class="btn-row">
        <button class="btn-abrir" onclick="accion('abrirSalida')">Abrir</button>
        <button class="btn-cerrar" onclick="accion('cerrarSalida')">Cerrar</button>
      </div>
    </div>
  </div>

  <div class="footer">ESP32 Access Point &bull; Actualiza cada 1s</div>

  <script>
    function formatTime(segundos) {
      const m = Math.floor(segundos / 60).toString().padStart(2, '0');
      const s = (segundos % 60).toString().padStart(2, '0');
      return m + ':' + s;
    }

    function actualizar() {
      fetch('/status')
        .then(function(res) { return res.json(); })
        .then(function(data) {
          for (var i = 0; i < 4; i++) {
            var space = document.getElementById('p' + i);
            var estado = document.getElementById('e' + i);
            var tiempo = document.getElementById('t' + i);

            if (data.p[i]) {
              space.className = 'space ocupada';
              estado.textContent = 'OCUPADA';
              tiempo.textContent = formatTime(data.t[i]);
            } else {
              space.className = 'space libre';
              estado.textContent = 'LIBRE';
              tiempo.textContent = '00:00';
            }
          }

          var barraE = document.getElementById('barra-e');
          var barraS = document.getElementById('barra-s');
          document.getElementById('st-e').textContent = data.e ? 'ABIERTA' : 'Cerrada';
          document.getElementById('st-s').textContent = data.s ? 'ABIERTA' : 'Cerrada';
          barraE.className = 'barra-visual' + (data.e ? ' abierta' : '');
          barraS.className = 'barra-visual' + (data.s ? ' abierta' : '');
        })
        .catch(function(err) { console.error('Error al actualizar:', err); });
    }

    function accion(endpoint) {
      fetch('/' + endpoint).then(actualizar).catch(function(err) { console.error(err); });
    }

    setInterval(actualizar, 1000);
    actualizar();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --------------------------------------------------------
// LCD I2C 16x2
// --------------------------------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --------------------------------------------------------
// Pines (TUS CONEXIONES reales — NO CAMBIAR)
// --------------------------------------------------------
// Motor Z (subida/bajada – husillo)
const int dirPinZ    = 4;
const int stepPinZ   = 5;
const int enablePinZ = 6;

// Motor R (giro base / dicker)
const int dirPinR    = 7;
const int stepPinR   = 8;
const int enablePinR = 9;

// Botones
const int botonSubir = 10;  // ↑
const int botonBajar = 11;  // ↓
const int botonSet   = 12;  // OK / siguiente / confirmar

// NUEVO: Final de carrera superior
const int finalCarreraSuperior = 13;  // Pin del final de carrera (cambia según tu conexión)

// --------------------------------------------------------
// Dirección lógica (ajusta si se invierte el sentido)
// --------------------------------------------------------
const bool Z_DIR_UP   = LOW;  // cambia a LOW si sube al revés
const bool Z_DIR_DOWN = HIGH;   // opuesto
const bool R_DIR_CW   = HIGH;  // giro horario
const bool R_DIR_CCW  = LOW;   // opuesto

// --------------------------------------------------------
// Parámetros ajustables (valores iniciales por defecto)
// --------------------------------------------------------
long pasosRecubrimiento = 80000; // profundidad (bajada) desde el tope
long pasosPorDicker     = 400;   // pasos para girar entre dickers

int numDickers          = 2;     // cantidad de dickers (baños)
int inmersionesPorDicker[10];    // inmersiones por cada dicker (max 10 dickers)
int tiempoInmersion[10];         // tiempo por inmersión en cada dicker
int tiempoSecado[10];            // tiempo de secado antes de cada inmersión

// Velocidades (µs entre flancos — menor = más rápido)
int velocidadZ = 100;   // husillo: rápido pero estable
int velocidadR = 2000;  // giro mucho más lento para precisión
int velocidadZLenta = 300;  // NUEVA: velocidad para homing (más rápida)

// NUEVO: Pasos de retroceso para posición final
int pasosRetrocesoZ = 1000;  // pasos antes del setpoint Z (ajustable)
int pasosRetrocesoR = 50;    // pasos antes del home de giro (ajustable)

// --------------------------------------------------------
// Estado del sistema
// --------------------------------------------------------
enum Estado {
  AUTO_HOME_Z,           // NUEVO: homing automático con final de carrera
  CONFIG_PROFUND,        // ajustar pasosRecubrimiento
  CALIB_ROT_HOME,        // posicionar giro dicker 1
  CONFIG_PASOS_DICKER,   // pasos entre dickers
  CONFIG_NUM_DICKERS,    // cantidad de dickers
  CONFIG_INMERSIONES,    // inmersiones por dicker
  CONFIG_TIEMPOS,        // tiempos de inmersión
  CONFIG_SECADO,         // tiempos de secado
  ESPERAR_RUN,           // pantalla de espera
  CICLO_RUN,             // ejecutando ciclo
  CICLO_TERMINADO        // nuevo estado al terminar ciclo
};

Estado estado = AUTO_HOME_Z;  // Cambiar estado inicial
int dickerActual = 0;  // para configurar inmersiones y tiempos

// Guardar posición zero (top) y giro base
long zTopPos   = 0;
long rHomePos  = 0;

// --------------------------------------------------------
// Utilidades de lectura de botones y final de carrera + SERIAL
// --------------------------------------------------------
// Variables para control serial
bool serialUpPressed = false;
bool serialDownPressed = false;
bool serialSetPressed = false;

// Función para leer comandos del monitor serial
void leerSerial() {
  if (Serial.available() > 0) {
    char comando = Serial.read();
    comando = toupper(comando); // Convertir a mayúscula
    
    switch (comando) {
      case 'W':
        serialUpPressed = true;
        Serial.println(">>> SUBIR/ARRIBA activado");
        break;
      case 'S':
        serialDownPressed = true;
        Serial.println(">>> BAJAR/ABAJO activado");
        break;
      case 'E':
        serialSetPressed = true;
        Serial.println(">>> SET/ENTER activado");
        break;
      default:
        // Ignorar otros caracteres
        break;
    }
  }
}

// Funciones mejoradas que combinan botones físicos y serial
inline bool upPressed() { 
  leerSerial();
  bool pressed = digitalRead(botonSubir) == HIGH || serialUpPressed;
  if (pressed && serialUpPressed) {
    serialUpPressed = false; // Reset después de usar
  }
  return pressed;
}

inline bool downPressed() { 
  leerSerial();
  bool pressed = digitalRead(botonBajar) == HIGH || serialDownPressed;
  if (pressed && serialDownPressed) {
    serialDownPressed = false; // Reset después de usar
  }
  return pressed;
}

inline bool setPressed() { 
  leerSerial();
  bool pressed = digitalRead(botonSet) == HIGH || serialSetPressed;
  if (pressed && serialSetPressed) {
    serialSetPressed = false; // Reset después de usar
  }
  return pressed;
}

// NUEVA: Función para leer final de carrera
// Asume que el final de carrera está conectado con pull-up interno
// y se activa a LOW cuando se presiona (configuración típica)
inline bool finalCarreraActivado() { 
  return digitalRead(finalCarreraSuperior) == LOW; 
}

// Pequeño debounce (mejorado para serial)
void waitReleaseAll() {
  delay(50);
  // Limpiar cualquier comando serial pendiente
  serialUpPressed = false;
  serialDownPressed = false;
  serialSetPressed = false;
  
  while (upPressed() || downPressed() || setPressed()) { 
    delay(10); 
    // Limpiar comandos serial durante la espera
    serialUpPressed = false;
    serialDownPressed = false;
    serialSetPressed = false;
  }
  delay(50);
}

// --------------------------------------------------------
// Pantallas rápidas (movidas aquí para estar disponibles antes)
// --------------------------------------------------------
void lcdMsg(const char* l1, const char* l2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
  
  // NUEVO: Mostrar también en monitor serial
  Serial.print("LCD: ");
  Serial.print(l1);
  if (strlen(l2) > 0) {
    Serial.print(" | ");
    Serial.print(l2);
  }
  Serial.println();
}

// Mostrar valor numérico en segunda línea (limpiando) + SERIAL
void lcdVal(const char* label, long v) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(label);
  lcd.setCursor(0, 1); lcd.print(v);
  
  // NUEVO: Mostrar también en monitor serial
  Serial.print(label);
  Serial.print(" ");
  Serial.println(v);
}

// Mostrar valor int (tiempos) + SERIAL
void lcdValI(const char* label, int v) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(label);
  lcd.setCursor(0, 1); lcd.print(v);
  
  // NUEVO: Mostrar también en monitor serial
  Serial.print(label);
  Serial.print(" ");
  Serial.println(v);
}

// Mostrar con información adicional + SERIAL
void lcdDicker(const char* label, int dicker, int valor) {
  lcd.clear();
  lcd.setCursor(0, 0); 
  lcd.print(label);
  lcd.print(" D");
  lcd.print(dicker + 1);
  lcd.setCursor(0, 1); 
  lcd.print(valor);
  
  // NUEVO: Mostrar también en monitor serial
  Serial.print(label);
  Serial.print(" D");
  Serial.print(dicker + 1);
  Serial.print(": ");
  Serial.println(valor);
}

// --------------------------------------------------------
// Stepping de bajo nivel
// --------------------------------------------------------
inline void pulseStep(int pin, int usDelay) {
  digitalWrite(pin, HIGH);
  delayMicroseconds(usDelay);
  digitalWrite(pin, LOW);
  delayMicroseconds(usDelay);
}

// NUEVA: Función de homing automático
bool homeZAxis() {
  lcdMsg("Buscando home Z", "Subiendo lento...");
  
  // Si ya está en el final de carrera, bajar un poco primero
  if (finalCarreraActivado()) {
    digitalWrite(dirPinZ, Z_DIR_DOWN);
    for (int i = 0; i < 1000 && finalCarreraActivado(); i++) {
      pulseStep(stepPinZ, velocidadZLenta);
    }
    delay(100);
  }
  
  // Configurar dirección hacia arriba
  digitalWrite(dirPinZ, Z_DIR_UP);
  
  // Subir lentamente hasta activar el final de carrera
  long maxSteps = 200000; // Límite de seguridad
  long stepCount = 0;
  
  while (!finalCarreraActivado() && stepCount < maxSteps) {
    pulseStep(stepPinZ, velocidadZLenta);
    stepCount++;
    
    // Actualizar LCD cada 1000 pasos
    if (stepCount % 1000 == 0) {
      lcd.setCursor(0, 1);
      lcd.print("Pasos: ");
      lcd.print(stepCount);
      lcd.print("    "); // Limpiar caracteres extra
    }
  }
  
  if (finalCarreraActivado()) {
    // Home encontrado exitosamente
    zTopPos = 0; // Establecer posición de referencia
    lcdMsg("Home Z encontrado", "Posicion guardada");
    delay(1000);
    return true;
  } else {
    // Error: no se encontró el home
    lcdMsg("ERROR: Home Z", "no encontrado");
    delay(2000);
    return false;
  }
}

// Movimiento relativo bloqueante con verificación de final de carrera
void moveStepsZ(long steps, bool up) {
  digitalWrite(dirPinZ, up ? Z_DIR_UP : Z_DIR_DOWN);
  for (long i = 0; i < steps; i++) {
    // Solo verificar final de carrera si está subiendo
    if (up && finalCarreraActivado()) {
      break;
    }
    pulseStep(stepPinZ, velocidadZ);
  }
}

// Nueva función: Movimiento Z sin verificación de final de carrera (para inmersiones)
void moveStepsZSinLimite(long steps, bool up) {
  digitalWrite(dirPinZ, up ? Z_DIR_UP : Z_DIR_DOWN);
  for (long i = 0; i < steps; i++) {
    pulseStep(stepPinZ, velocidadZ);
  }
}

void moveStepsR(long steps, bool cw) {
  digitalWrite(dirPinR, cw ? R_DIR_CW : R_DIR_CCW);
  for (long i = 0; i < steps; i++) {
    pulseStep(stepPinR, velocidadR);
  }
}

// Movimiento continuo manual mientras botón presionado (con protección)
void jogZ(bool up) {
  digitalWrite(dirPinZ, up ? Z_DIR_UP : Z_DIR_DOWN);
  // mientras el botón siga presionado, emitir pasos
  while ((up && upPressed()) || (!up && downPressed())) {
    // Si está subiendo y se activa el final de carrera, detener
    if (up && finalCarreraActivado()) {
      break;
    }
    pulseStep(stepPinZ, velocidadZ);
  }
}

// Jog giro manual
void jogR(bool cw) {
  digitalWrite(dirPinR, cw ? R_DIR_CW : R_DIR_CCW);
  while ((cw && upPressed()) || (!cw && downPressed())) {
    pulseStep(stepPinR, velocidadR);
  }
}

// (Funciones LCD movidas arriba)

// --------------------------------------------------------
// Inicializar valores por defecto
// --------------------------------------------------------
void inicializarDefaults() {
  for (int i = 0; i < 10; i++) {
    inmersionesPorDicker[i] = 1;  // 1 inmersión por defecto
    tiempoInmersion[i] = 5;       // 5 segundos por defecto
    tiempoSecado[i] = 3;          // 3 segundos de secado por defecto
  }
}

// --------------------------------------------------------
// SETUP
// --------------------------------------------------------
void setup() {
  // NUEVO: Inicializar comunicación serial
  Serial.begin(9600);
  Serial.println("=== Deepool Coater ===");
  Serial.println("Controles por teclado:");
  Serial.println("W = Subir/Arriba");
  Serial.println("S = Bajar/Abajo"); 
  Serial.println("E = Set/Enter/OK");
  Serial.println("===================================");
  
  // Motores
  pinMode(dirPinZ, OUTPUT);
  pinMode(stepPinZ, OUTPUT);
  pinMode(enablePinZ, OUTPUT); digitalWrite(enablePinZ, LOW);

  pinMode(dirPinR, OUTPUT);
  pinMode(stepPinR, OUTPUT);
  pinMode(enablePinR, OUTPUT); digitalWrite(enablePinR, LOW);

  // Botones
  pinMode(botonSubir, INPUT);  // pull-down externo
  pinMode(botonBajar, INPUT);
  pinMode(botonSet, INPUT);

  // NUEVO: Final de carrera con pull-up interno
  pinMode(finalCarreraSuperior, INPUT_PULLUP);

  // LCD
  lcd.init();
  lcd.backlight();
  
  // Inicializar valores
  inicializarDefaults();
  
  lcdMsg("Deep Coater", "Multi-Dicker v2");
  delay(1200);
}

// --------------------------------------------------------
// LOOP
// --------------------------------------------------------
void loop() {
  switch (estado) {

    // ---------- NUEVO: AUTO HOME Z ----------
    case AUTO_HOME_Z: {
      if (homeZAxis()) {
        estado = CONFIG_PROFUND;
      } else {
        // Si falla el homing, permitir reintentar con botón SET
        lcdMsg("Presiona SET para", "reintentar homing");
        while (!setPressed()) { delay(10); }
        waitReleaseAll();
      }
    } break;

    // ---------- CONFIG TIEMPOS DE SECADO ----------
    case CONFIG_SECADO: {
      lcdDicker("Secado (s)", dickerActual, tiempoSecado[dickerActual]);
      while (true) {
        if (upPressed())   { tiempoSecado[dickerActual]++; 
                             lcdDicker("Secado (s)", dickerActual, tiempoSecado[dickerActual]); delay(150); }
        if (downPressed()) { if (tiempoSecado[dickerActual] > 0) tiempoSecado[dickerActual]--;
                             lcdDicker("Secado (s)", dickerActual, tiempoSecado[dickerActual]); delay(150); }
        if (setPressed())  { 
          waitReleaseAll(); 
          dickerActual++;
          if (dickerActual >= numDickers) {
            estado = ESPERAR_RUN;
          }
          break; 
        }
      }
    } break;

    // ---------- CONFIG PROFUNDIDAD ----------
    case CONFIG_PROFUND: {
      lcdVal("Prof (pasos):", pasosRecubrimiento);
      while (true) {
        if (upPressed())   { pasosRecubrimiento += 1000; lcdVal("Prof (pasos):", pasosRecubrimiento); delay(150); }
        if (downPressed()) { if (pasosRecubrimiento > 1000) pasosRecubrimiento -= 1000;
                             lcdVal("Prof (pasos):", pasosRecubrimiento); delay(150); }
        if (setPressed())  { waitReleaseAll(); estado = CALIB_ROT_HOME; break; }
      }
    } break;

    // ---------- CALIBRAR GIRO HOME ----------
        // ---------- CALIBRAR GIRO HOME ----------
    case CALIB_ROT_HOME: {
      lcdMsg("Calibra GIRO 1", "W/S = mover, E = OK");
      Serial.println("=== MODO CALIBRACION GIRO ===");
      Serial.println("Usa W (horario), S (antihorario), E (Set)");
      
      while (true) {
        if (upPressed()) {
          Serial.println("Girando sentido horario (CW)");
          jogR(true);
        }
        if (downPressed()) {
          Serial.println("Girando sentido antihorario (CCW)");
          jogR(false);
        }
        if (setPressed()) {
          waitReleaseAll();
          rHomePos = 0;
          lcdMsg("Giro1 fijado");
          Serial.println("✔ Giro base 1 fijado en posición actual");
          delay(800);
          estado = CONFIG_PASOS_DICKER;
          break;
        }
      }
    } break;


    // ---------- CONFIG PASOS ENTRE DICKERS ----------
    case CONFIG_PASOS_DICKER: {
      lcdVal("Pasos/dicker:", pasosPorDicker);
      while (true) {
        if (upPressed())   { pasosPorDicker += 10; lcdVal("Pasos/dicker:", pasosPorDicker); delay(120); }
        if (downPressed()) { if (pasosPorDicker > 10) pasosPorDicker -= 10;
                             lcdVal("Pasos/dicker:", pasosPorDicker); delay(120); }
        if (setPressed())  { waitReleaseAll(); estado = CONFIG_NUM_DICKERS; break; }
      }
    } break;

    // ---------- CONFIG NUMERO DE DICKERS ----------
    case CONFIG_NUM_DICKERS: {
      lcdValI("Num Dickers:", numDickers);
      while (true) {
        if (upPressed())   { if (numDickers < 10) numDickers++; lcdValI("Num Dickers:", numDickers); delay(150); }
        if (downPressed()) { if (numDickers > 1) numDickers--; lcdValI("Num Dickers:", numDickers); delay(150); }
        if (setPressed())  { waitReleaseAll(); dickerActual = 0; estado = CONFIG_INMERSIONES; break; }
      }
    } break;

    // ---------- CONFIG INMERSIONES POR DICKER ----------
    case CONFIG_INMERSIONES: {
      lcdDicker("Inmersiones", dickerActual, inmersionesPorDicker[dickerActual]);
      while (true) {
        if (upPressed())   { if (inmersionesPorDicker[dickerActual] < 10) inmersionesPorDicker[dickerActual]++;
                             lcdDicker("Inmersiones", dickerActual, inmersionesPorDicker[dickerActual]); delay(150); }
        if (downPressed()) { if (inmersionesPorDicker[dickerActual] > 1) inmersionesPorDicker[dickerActual]--;
                             lcdDicker("Inmersiones", dickerActual, inmersionesPorDicker[dickerActual]); delay(150); }
        if (setPressed())  { 
          waitReleaseAll(); 
          dickerActual++;
          if (dickerActual >= numDickers) {
            dickerActual = 0;
            estado = CONFIG_TIEMPOS;
          }
          break; 
        }
      }
    } break;

    // ---------- CONFIG TIEMPOS DE INMERSION ----------
    case CONFIG_TIEMPOS: {
      lcdDicker("Tiempo (s)", dickerActual, tiempoInmersion[dickerActual]);
      while (true) {
        if (upPressed())   { tiempoInmersion[dickerActual]++; 
                             lcdDicker("Tiempo (s)", dickerActual, tiempoInmersion[dickerActual]); delay(150); }
        if (downPressed()) { if (tiempoInmersion[dickerActual] > 1) tiempoInmersion[dickerActual]--;
                             lcdDicker("Tiempo (s)", dickerActual, tiempoInmersion[dickerActual]); delay(150); }
        if (setPressed())  { 
          waitReleaseAll(); 
          dickerActual++;
          if (dickerActual >= numDickers) {
            dickerActual = 0;
            estado = CONFIG_SECADO;
          }
          break; 
        }
      }
    } break;

    // ---------- ESPERA INICIO ----------
    case ESPERAR_RUN: {
      lcdMsg("Listo ciclo", "SET para iniciar");
      if (setPressed()) {
        waitReleaseAll();
        estado = CICLO_RUN;
      }
    } break;

    // ---------- CICLO AUTOMÁTICO ----------
    case CICLO_RUN: {
      // Ejecutar ciclo para cada dicker
      for (int d = 0; d < numDickers; d++) {
        // Si no es el primer dicker, girar a la siguiente posición
        if (d > 0) {
          lcdMsg("Girando a D", String(d + 1).c_str());
          moveStepsR(pasosPorDicker, true); // girar horario
        }
        
        // Realizar las inmersiones en este dicker
        for (int inm = 0; inm < inmersionesPorDicker[d]; inm++) {
          // Mostrar progreso
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("D"); lcd.print(d + 1); lcd.print(" Inm"); lcd.print(inm + 1);
          lcd.setCursor(0, 1);
          
          if (inm == 0) {
            // Primera inmersión: bajar completamente
            lcd.print("Bajando...");
            moveStepsZSinLimite(pasosRecubrimiento, false);
          } else {
            // Inmersiones posteriores: bajar solo la mitad (lo que se subió)
            lcd.print("Bajando 1/2...");
            moveStepsZSinLimite(pasosRecubrimiento / 2, false);
          }
          
          // Tiempo de inmersión
          countdown2("Inmersion", d + 1, tiempoInmersion[d]);
          
          // Subir a la mitad SIEMPRE (para secar)
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("D"); lcd.print(d + 1); lcd.print(" Inm"); lcd.print(inm + 1);
          lcd.setCursor(0, 1);
          lcd.print("Subida parcial");
          moveStepsZSinLimite(pasosRecubrimiento / 2, true);
          
          // Tiempo de secado SIEMPRE (en todas las inmersiones)
          if (tiempoSecado[d] > 0) {
            countdown2("Secando D", d + 1, tiempoSecado[d]);
          }
          
          // Si es la ÚLTIMA inmersión, subir la mitad restante para llegar al tope
          if (inm == inmersionesPorDicker[d] - 1) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("D"); lcd.print(d + 1); lcd.print(" Final");
            lcd.setCursor(0, 1);
            lcd.print("Subiendo al tope");
            moveStepsZ(pasosRecubrimiento / 2, true); // Esta SÍ con límite para protección
          }
        }
      }
      
      // Volver a posición inicial (dicker 1)
      if (numDickers > 1) {
        lcdMsg("Volviendo a D1", "");
        moveStepsR(pasosPorDicker * (numDickers - 1), false); // girar antihorario
      }
      
      // NUEVA FUNCIONALIDAD: Posicionamiento final
      // Retroceder en Z para quedar antes del setpoint
      if (pasosRetrocesoZ > 0) {
        lcdMsg("Posicion final Z", "Bajando...");
        moveStepsZSinLimite(pasosRetrocesoZ, false);
      }
      
      // Retroceder en giro para quedar antes del home
      if (pasosRetrocesoR > 0) {
        lcdMsg("Posicion final R", "Ajustando...");
        moveStepsR(pasosRetrocesoR, false); // retroceder antihorario
      }
      
      lcdMsg("Ciclo completo", "Posicion final OK");
      delay(1500);
      
      // Cambiar al nuevo estado de terminado
      estado = CICLO_TERMINADO;
    } break;

    // ---------- NUEVO ESTADO: CICLO TERMINADO ----------
    case CICLO_TERMINADO: {
      lcdMsg("Ciclo listo!", "SET=otra vez");
      
      // Verificar botones
      if (setPressed()) {
        waitReleaseAll();
        estado = ESPERAR_RUN;  // Repetir ciclo
      }
      // NUEVA FUNCIONALIDAD: Volver a calibración
      else if (upPressed() || downPressed()) {
        waitReleaseAll();
        lcdMsg("Regresando...", "");
        delay(500);
        estado = AUTO_HOME_Z;  // Volver al homing automático
      }
    } break;
  }
}

// --------------------------------------------------------
// Cuenta regresiva en pantalla (segundos) - versión mejorada
// --------------------------------------------------------
void countdown(const char* label, int segundos) {
  for (int s = segundos; s > 0; s--) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(label);
    lcd.setCursor(0, 1);
    lcd.print("Restan: ");
    lcd.print(s);
    lcd.print("s");
    delay(1000);
  }
}

// Cuenta regresiva con información del dicker + SERIAL
void countdown2(const char* label, int dicker, int segundos) {
  Serial.print("INICIANDO: ");
  Serial.print(label);
  Serial.print(" D");
  Serial.print(dicker);
  Serial.print(" por ");
  Serial.print(segundos);
  Serial.println(" segundos");
  
  for (int s = segundos; s > 0; s--) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(label); lcd.print(" D"); lcd.print(dicker);
    lcd.setCursor(0, 1);
    lcd.print("Restan: ");
    lcd.print(s);
    lcd.print("s");
    
    // Mostrar cada 5 segundos en serial, o los últimos 3
    if (s % 5 == 0 || s <= 3) {
      Serial.print("Restan: ");
      Serial.print(s);
      Serial.println("s");
    }
    
    delay(1000);
  }
  
  Serial.print("COMPLETADO: ");
  Serial.print(label);
  Serial.print(" D");
  Serial.println(dicker);
}
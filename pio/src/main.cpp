#include <Arduino.h>
#include <IRremote.h>

// === Настройки ===
#define SERIAL_BAUD       115200
#define V_REF             5.0

// === Пины ===
#define PIN_NTC           A0
#define PIN_BUTTONS       A2
#define PIN_IR            8
#define PIN_SDI           2
#define PIN_CLK           3
#define PIN_LATCH         5
#define PIN_OE            6   // Новый пин: OE (активный при LOW)
#define PIN_RED           10
#define PIN_GRN           9

// === Яркость (0–100%) ===
int brightnessPercent = 90;  // от 0 до 100%
int pwmValue = 0;           // 0..255

#define LED_PWM           90 // Яркость верхних ламп. Нельзя больше 100!

// === Параметры NTC ===
#define SERIES_RESISTOR   10000
#define NOMINAL_RESISTANCE 10000
#define NOMINAL_TEMPERATURE 25
#define B_COEFFICIENT     3950

// === Режимы обновления ===
#define MODE_BIT          0x01  // после каждого бита
#define MODE_REGISTER     0x02  // после 16 бит
#define MODE_ALL          0x03  // после 80 бит
#define MODE_HOLD         0x00  // только загрузить

#define CMD_0             0xEF101DCC // Кнопка 3
#define CMD_1             0xEE111DCC // Кнопка 1
#define CMD_2             0xED121DCC // Кнопка 2
#define CMD_3             0xEC131DCC // Кнопка 3
#define CMD_4             0xEB141DCC // Кнопка 4
#define CMD_5             0xEA151DCC // Кнопка 5
#define CMD_6             0xE9161DCC // Кнопка 6
#define CMD_7             0xE8171DCC // Кнопка 7
#define CMD_8             0xE7181DCC // Кнопка 8
#define CMD_9             0xE6191DCC // Кнопка 9
#define CMD_RST           0xFF001DCC // Кнопка RESET
#define CMD_OFF           0xFE011DCC // Кнопка ON/OFF
#define CMD_OK            0xFB041DCC // Кнопка ОК
#define CMD_TIMER         0xDC231DCC // Кнопка с часами
#define CMD_LEFT          0xF8071DCC // Кнопка влево
#define CMD_RIGHT         0xF7081DCC // Кнопка вправо
#define CMD_UP            0xF8051DCC // Кнопка вверх
#define CMD_DOWN          0xF8061DCC // Кнопка вниз

// === Буферы ===
byte displayData[10];
int currentMode = MODE_HOLD;

// === Состояния ===
volatile bool dataReady = false;
unsigned long lastUpdate = 0;
unsigned long lastUpdateX = 0;

// current timer value
byte timerx[6] = {0,0,0,0,0,0};

// current output
byte current[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Bool to show tickling seconds
bool tickler = true;
bool flashPhase = false;
bool displayDirty = false;

// === LED modes for steps ===
#define LED_MODE_OFF         0
#define LED_MODE_GREEN       1
#define LED_MODE_RED         2
#define LED_MODE_FLASH_GREEN 3
#define LED_MODE_FLASH_RED   4
#define LED_MODE_YELLOW      5
#define LED_MODE_FLASH_YELLOW 6

// === Program definitions ===
struct Step {
  byte hh_tens,hh_ones,mm_tens, mm_ones, ss_tens, ss_ones;
  byte ledMode;
};

struct Program {
  byte numCycles;
  byte numSteps;
  byte doneMode;   // LED mode when program finishes (LED_MODE_OFF to turn off)
  Step steps[4];
};

const Program programs[] = {
  // Program 1: 4 cycles of (0:10 work green, 0:10 flash red, stops off)
  { 4, 2, LED_MODE_OFF, {
    { 0, 0, 1, 5, 0, 0, LED_MODE_GREEN },
    { 0, 0, 0, 1, 0, 0, LED_MODE_FLASH_RED },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF }
  }},
  // Program 2: 10s green, 10s flash yellow, stop with red
  { 1, 2, LED_MODE_RED, {
    { 0, 0, 0, 5, 0, 0, LED_MODE_GREEN },
    { 0, 0, 0, 1, 0, 0, LED_MODE_FLASH_YELLOW },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF }
  }},
  // Program 3: 10s green, stop with red
  { 1, 1, LED_MODE_RED, {
    { 0, 2, 0, 0, 0, 0, LED_MODE_GREEN },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF }
  }},
  // Program 4: 10s green, stops with red
  { 1, 1, LED_MODE_RED, {
    { 0, 1, 4, 0, 0, 0, LED_MODE_GREEN },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF },
    { 0, 0, 0, 0, 0, 0, LED_MODE_OFF }
  }}
};

// Runtime state
byte curProgram = 0;   // 0 = idle, 1-4 = active program index
byte curCycle = 0;
byte curStep = 0;
byte curLedMode = LED_MODE_OFF;

// Unused bits
// current[2] |= (1 << 0); - minus sign

// coding for seconds
byte sLeft[10][2] = {
  {0x0E, 0x38}, // 0x
  {0x08, 0x08}, // 1x
  {0x06, 0x58}, // 2x
  {0x0C, 0x58}, // 3x
  {0x08, 0x68}, // 4x
  {0x0C, 0x70}, // 5x
  {0x0E, 0x70}, // 6x
  {0x08, 0x18}, // 7x
  {0x0E, 0x78}, // 8x
  {0x0C, 0x78}  // 9x
};

byte sRight[10][2] = {
  {0x70, 0x07}, // x0
  {0x40, 0x01}, // x1
  {0xB0, 0x05}, // x2
  {0xE0, 0x05}, // x3
  {0xC0, 0x03}, // x4
  {0xE0, 0x06}, // x5
  {0xF0, 0x06}, // x6
  {0x40, 0x05}, // x7
  {0xF0, 0x07}, // x8
  {0xE0, 0x07}  // x9
};

// coding for hours and minutes
byte mLeft[10][2] = {
  {0x0E, 0x38}, // 0x
  {0x08, 0x08}, // 1x
  {0x06, 0x58}, // 2x
  {0x0C, 0x58}, // 3x
  {0x08, 0x68}, // 4x
  {0x0C, 0x70}, // 5x
  {0x0E, 0x70}, // 6x
  {0x08, 0x18}, // 7x
  {0x0E, 0x78}, // 8x
  {0x0C, 0x78}  // 9x
};

byte mRight[10][2] = {
  {0x70, 0x07}, // x0
  {0x40, 0x01}, // x1
  {0xB0, 0x03}, // x2
  {0xE0, 0x03}, // x3
  {0xC0, 0x05}, // x4
  {0xE0, 0x06}, // x5
  {0xF0, 0x06}, // x6
  {0x40, 0x03}, // x7
  {0xF0, 0x07}, // x8
  {0xE0, 0x07}  // x9
};

// === Прототипы ===
void startAnimation();
void sendDataWithMode(byte data[10], int mode);
void parseSerialInput();
void printIRResult();
float calculateTemperature(int rawADC);
void updateShiftRegister(bool bit);
void pulseLatch();
void updateBrightness();
void applyLedMode(byte mode, bool flashPhase);
void loadCurrentStep();
void updateDisplay();

void setup() {
  // === Инициализация пинов ===
  pinMode(PIN_SDI,     OUTPUT);
  pinMode(PIN_CLK,     OUTPUT);
  pinMode(PIN_LATCH,   OUTPUT);
  //pinMode(PIN_OE,      OUTPUT);
  //pinMode(PIN_RED,     OUTPUT);
  //pinMode(PIN_GRN,     OUTPUT);

  digitalWrite(PIN_SDI,     LOW);
  digitalWrite(PIN_CLK,     LOW);
  digitalWrite(PIN_LATCH,   LOW);
  
  pinMode(PIN_OE, OUTPUT);  // важен порядок для analogWrite
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GRN, OUTPUT);

  // === Установка яркости ===
  updateBrightness();

  // === Serial ===
  Serial.begin(SERIAL_BAUD);
  delay(1000);
  Serial.println("=== УСТРОЙСТВО ГОТОВО ===");
  Serial.println("Ожидание стартовой анимации...");

  IrReceiver.begin(PIN_IR, ENABLE_LED_FEEDBACK);

  // === Стартовая анимация ===
  startAnimation();

  current[9] = sLeft[0][1] ^ sRight[0][1];
  current[8] = sLeft[0][0] ^ sRight[0][0];
  current[7] = mLeft[0][1] ^ mRight[0][1];
  current[6] = mLeft[0][0] ^ mRight[0][0];
  current[5] = mLeft[0][1] ^ mRight[0][1];
  current[4] = mLeft[0][0] ^ mRight[0][0];
  
  sendDataWithMode(current, MODE_ALL);
}

void loop() {
  unsigned long now = millis();

  // Cycle every 0.5s for ticking points
  if (now - lastUpdateX >= 500 ) {
    lastUpdateX = now;

    // Toggle flash phase every 500ms
    flashPhase = !flashPhase;

    // Blink colon separators when timer is running
    if (timerx[5] || timerx[4] || timerx[3] || timerx[2] || timerx[1] || timerx[0]) {
      if (flashPhase) {
        current[9] |= (1 << 7);
        current[7] |= (1 << 7);
      } else {
        current[9] &= ~(1 << 7);
        current[7] &= ~(1 << 7);
      }
      displayDirty = true;
    }

    // Toggle flashing LEDs every 500ms
    applyLedMode(curLedMode, flashPhase);

    // Try to get any IR input
    if (IrReceiver.decode()) {
      switch (IrReceiver.decodedIRData.decodedRawData) {
        case CMD_1:
          curProgram = 1; curCycle = 0; curStep = 0;
          loadCurrentStep();
          break;
        case CMD_2:
          curProgram = 2; curCycle = 0; curStep = 0;
          loadCurrentStep();
          break;
        case CMD_3:
          curProgram = 3; curCycle = 0; curStep = 0;
          loadCurrentStep();
          break;
        case CMD_4:
          curProgram = 4; curCycle = 0; curStep = 0;
          loadCurrentStep();
          break;
        case CMD_RST:
          curProgram = 0; curCycle = 0; curStep = 0;
          curLedMode = LED_MODE_OFF;
          applyLedMode(LED_MODE_OFF, false);
          timerx[0] = 0; timerx[1] = 0; timerx[2] = 0;
          timerx[3] = 0; timerx[4] = 0; timerx[5] = 0;
          updateDisplay();
          break;
        case CMD_LEFT:
          current[3] ^= (1 << 7);
          updateDisplay();
          break;
        case CMD_RIGHT:
          current[2] ^= (1 << 2);
          updateDisplay();
          break;
        default:
          IrReceiver.printIRResultShort(&Serial); 
          break;
      }
      IrReceiver.printIRResultShort(&Serial); 
      IrReceiver.resume();
    }

/*    for (int i = 0; i < 6; i++) {
      Serial.print("0x"); // Print "0x" prefix for hexadecimal representation
      if (timerx[i] < 0x10) { // Add leading zero for single-digit hex values
        Serial.print("0");
      }
      Serial.print(timerx[i], HEX); // Print the byte in hexadecimal format
      Serial.print(" "); // Add a space for readability
    }
    for (int i = 0; i < 10; i++) {
      Serial.print("0x"); // Print "0x" prefix for hexadecimal representation
      if (current[i] < 0x10) { // Add leading zero for single-digit hex values
        Serial.print("0");
      }
      Serial.print(current[i], HEX); // Print the byte in hexadecimal format
      Serial.print(" "); // Add a space for readability
    }
    Serial.println();*/
  }

  
  // Checking if zero was not reached for real timer
  if (timerx[5] || timerx[4] || timerx[3] || timerx[2] || timerx[1] || timerx[0]) {

    // Cycle for every second
    if (now - lastUpdate >= 1000) {
      lastUpdate = now;

      timerx[5]--;

      if (timerx[5] > 9) {
        timerx[5] = 9;
        timerx[4] --;
        if (timerx[4] > 9) {
          timerx[4] = 5;
          timerx[3]--;
          if (timerx[3] > 9) {
            timerx[3] = 9;
            timerx[2]--;
            if (timerx[2] > 9) {
              timerx[2] = 5;
              timerx[1]--;
              if (timerx[1] > 9) {
                timerx[1] = 9;
                timerx[0]--;
              }
            }
          }
        }
      }

      updateDisplay();
    }
  } else if (curProgram > 0) {
    // Timer reached zero — advance to next step/cycle
    curStep++;
    if (curStep >= programs[curProgram - 1].numSteps) {
      curStep = 0;
      curCycle++;
      if (curCycle >= programs[curProgram - 1].numCycles) {
        // Program complete — apply done LED mode
        byte doneMode = programs[curProgram - 1].doneMode;
        curProgram = 0;
        curLedMode = doneMode;
        applyLedMode(doneMode, false);
        // Display zeros
        timerx[0] = 0; timerx[1] = 0; timerx[2] = 0;
        timerx[3] = 0; timerx[4] = 0; timerx[5] = 0;
        updateDisplay();
        return;
      }
    }
    loadCurrentStep();
  }

  // Send display data once per loop if anything changed
  if (displayDirty) {
    sendDataWithMode(current, MODE_ALL);
    displayDirty = false;
  }
}

// ============ ФУНКЦИИ ============

// === Обновление яркости (0–100% → 0–255) ===
void updateBrightness() {
  pwmValue = (brightnessPercent * 255) / 100;
  analogWrite(PIN_OE, 255 - pwmValue);  // OE активен при LOW → инвертируем!
  // Пояснение: чем больше ШИМ на PIN_OE, тем реже LOW → тем темнее
  // analogWrite(255) → всегда HIGH → выключено
  // analogWrite(0)   → всегда LOW → включено
  // Мы хотим: 30% яркости → 30% времени LOW → 70% времени HIGH → значит, ШИМ = 255 - (30*255/100)
}

// === Стартовая анимация ===
void startAnimation() {
  byte on[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  byte off[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  for (int i = 0; i < 10; i++) displayData[i] = on[i];
  sendDataWithMode(displayData, MODE_BIT);
  delay(500);
  for (int i = 0; i < 10; i++) displayData[i] = off[i];
  sendDataWithMode(displayData, MODE_BIT);
}

// === Fast shift out using direct port manipulation ===
// PIN_SDI=2 (PD2), PIN_CLK=3 (PD3), PIN_LATCH=5 (PD5)
#define SDI_BIT   (1 << 2)
#define CLK_BIT   (1 << 3)
#define LATCH_BIT (1 << 5)

inline void shiftOutFast(byte val) {
  for (int8_t i = 7; i >= 0; i--) {
    if (val & (1 << i)) PORTD |= SDI_BIT; else PORTD &= ~SDI_BIT;
    PORTD |= CLK_BIT;
    PORTD &= ~CLK_BIT;
  }
}

// === Отправка данных ===
void sendDataWithMode(byte data[10], int mode) {
  if (mode == MODE_ALL) {
    // Shift all 80 bits with display still on — output doesn't change until latch
    for (int b = 0; b < 10; b++) {
      shiftOutFast(data[b]);
    }
    // Brief blank, latch, restore
    analogWrite(PIN_OE, 255);
    PORTD |= LATCH_BIT;
    PORTD &= ~LATCH_BIT;
    analogWrite(PIN_OE, 255 - pwmValue);
  } else {
    // MODE_BIT / MODE_REGISTER: blank during shift (used for animation only)
    analogWrite(PIN_OE, 255);
    int bitCount = 0;
    for (int b = 0; b < 10; b++) {
      for (int i = 7; i >= 0; i--) {
        if (data[b] & (1 << i)) PORTD |= SDI_BIT; else PORTD &= ~SDI_BIT;
        PORTD |= CLK_BIT;
        PORTD &= ~CLK_BIT;
        bitCount++;

        bool shouldUpdate = false;
        if (mode == MODE_BIT) shouldUpdate = true;
        else if (mode == MODE_REGISTER && (bitCount % 16) == 0) shouldUpdate = true;

        if (shouldUpdate) {
          PORTD |= LATCH_BIT;
          PORTD &= ~LATCH_BIT;
          analogWrite(PIN_OE, 255 - pwmValue);
        }
      }
    }
  }
}

// === Обновление одного бита (kept for compatibility) ===
void updateShiftRegister(bool bit) {
  if (bit) PORTD |= SDI_BIT; else PORTD &= ~SDI_BIT;
  PORTD |= CLK_BIT;
  PORTD &= ~CLK_BIT;
}

// === Latch ===
void pulseLatch() {
  PORTD |= LATCH_BIT;
  PORTD &= ~LATCH_BIT;
}

// === Apply LED mode ===
void applyLedMode(byte mode, bool flashPhase) {
  switch (mode) {
    case LED_MODE_OFF:
      analogWrite(PIN_RED, 0);
      analogWrite(PIN_GRN, 0);
      break;
    case LED_MODE_GREEN:
      analogWrite(PIN_RED, 0);
      analogWrite(PIN_GRN, LED_PWM);
      break;
    case LED_MODE_RED:
      analogWrite(PIN_RED, LED_PWM);
      analogWrite(PIN_GRN, 0);
      break;
    case LED_MODE_FLASH_GREEN:
      analogWrite(PIN_RED, 0);
      analogWrite(PIN_GRN, flashPhase ? LED_PWM : 0);
      break;
    case LED_MODE_FLASH_RED:
      analogWrite(PIN_RED, flashPhase ? LED_PWM : 0);
      analogWrite(PIN_GRN, 0);
      break;
    case LED_MODE_YELLOW:
      analogWrite(PIN_RED, LED_PWM);
      analogWrite(PIN_GRN, LED_PWM);
      break;
    case LED_MODE_FLASH_YELLOW:
      analogWrite(PIN_RED, flashPhase ? LED_PWM : 0);
      analogWrite(PIN_GRN, flashPhase ? LED_PWM : 0);
      break;
  }
}

// === Load current step into timer and LEDs ===
void loadCurrentStep() {
  const Step &s = programs[curProgram - 1].steps[curStep];
  timerx[0] = s.hh_tens;
  timerx[1] = s.hh_ones;
  timerx[2] = s.mm_tens;
  timerx[3] = s.mm_ones;
  timerx[4] = s.ss_tens;
  timerx[5] = s.ss_ones;
  curLedMode = s.ledMode;
  applyLedMode(curLedMode, true);
  updateDisplay();
}

// === Update display from timerx ===
void updateDisplay() {
  current[9] = sLeft[timerx[4]][1] ^ sRight[timerx[5]][1];
  current[8] = sLeft[timerx[4]][0] ^ sRight[timerx[5]][0];
  current[7] = mLeft[timerx[2]][1] ^ mRight[timerx[3]][1];
  current[6] = mLeft[timerx[2]][0] ^ mRight[timerx[3]][0];
  current[5] = mLeft[timerx[0]][1] ^ mRight[timerx[1]][1];
  current[4] = mLeft[timerx[0]][0] ^ mRight[timerx[1]][0];
  // Re-apply colon bits
  if (flashPhase) {
    current[9] |= (1 << 7);
    current[7] |= (1 << 7);
  }
  displayDirty = true;
}

// === Парсинг Serial ===
void parseSerialInput() {
  static String input = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (input.length() > 0) {
        int values[11];
        int count = 0;
        String token = "";

        for (int i = 0; i < input.length(); i++) {
          char ch = input[i];
          if (ch == ',' || ch == ' ' || ch == '\t') {
            if (token.startsWith("0x") || token.startsWith("0X")) {
              values[count++] = (int)strtol(token.c_str(), NULL, 16);
            }
            token = "";
          } else {
            token += ch;
          }
        }
        if (token.length() > 0 && (token.startsWith("0x") || token.startsWith("0X"))) {
          values[count++] = (int)strtol(token.c_str(), NULL, 16);
        }

        if (count == 11) {
          for (int i = 0; i < 10; i++) {
            displayData[i] = (byte)values[i];
          }
          currentMode = values[10];
          dataReady = true;
          Serial.println("Данные приняты.");
        } else {
          Serial.print("Ошибка: нужно 11 байт. Получено: ");
          Serial.println(count);
        }
        input = "";
      }
    } else {
      input += c;
    }
  }
}

// === Температура ===
float calculateTemperature(int rawADC) {
  if (rawADC <= 0 || rawADC >= 1023) return NAN;
  float R_thermistor = SERIES_RESISTOR * (1023.0 / rawADC - 1);
  float steinhart = log(R_thermistor / NOMINAL_RESISTANCE);
  steinhart /= B_COEFFICIENT;
  steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15);
  steinhart = 1.0 / steinhart;
  return steinhart - 273.15;
}

// === ИК ===
void printIRResult() {
  Serial.print(IrReceiver.decodedIRData.protocol);
  Serial.print(IrReceiver.decodedIRData.decodedRawData, HEX);
  Serial.println();
}

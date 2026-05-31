#include <Arduino.h>
#include <IRremote.h>

// === Настройки ===
#define SERIAL_BAUD       115200
#define V_REF             5.0

// === Пины ===
#define PIN_NTC           A0
#define PIN_BUTTONS       A2
#define PIN_IR            9
#define PIN_SDI           2
#define PIN_CLK           3
#define PIN_LATCH         5
#define PIN_OE            6   // Новый пин: OE (активный при LOW)

// === Яркость (0–100%) ===
int brightnessPercent = 3;  // от 0 до 100%
int pwmValue = 0;           // 0..255

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

// === Буферы ===
byte displayData[10];
int currentMode = MODE_HOLD;

// === Состояния ===
volatile bool dataReady = false;
unsigned long lastUpdate = 0;

// === IR ===
IRrecv irrecv(PIN_IR);
decode_results results;

byte timerx[6] = {0,1,0,0,0,0};

byte current[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

byte left[10][2] = {
  {0x0E, 0x38},
  {0x08, 0x08},
  {0x06, 0x58},
  {0x0C, 0x58},
  {0x08, 0x68},
  {0x0C, 0x70},
  {0x0E, 0x70},
  {0x08, 0x18},
  {0x0E, 0x78},
  {0x0C, 0x78}
};

byte right[10][2] = {
  {0x0E, 0x38},
  {0x40, 0x01},
  {0xB0, 0x03},
  {0xE0, 0x03},
  {0xC0, 0x05},
  {0xE0, 0x06},
  {0xF0, 0x06},
  {0x40, 0x03},
  {0x0E, 0x78},
  {0xE0, 0x07}
};

// === Прототипы ===
void startAnimation();
void sendDataWithMode(byte data[10], int mode);
void parseSerialInput();
void printIRResult(decode_results *res);
float calculateTemperature(int rawADC);
void updateShiftRegister(bool bit);
void pulseLatch();
void updateBrightness();  // обновление ШИМ по яркости

void setup() {
  // === Инициализация пинов ===
  pinMode(PIN_SDI,     OUTPUT);
  pinMode(PIN_CLK,     OUTPUT);
  pinMode(PIN_LATCH,   OUTPUT);
  pinMode(PIN_OE,      OUTPUT);

  digitalWrite(PIN_SDI,     LOW);
  digitalWrite(PIN_CLK,     LOW);
  digitalWrite(PIN_LATCH,   LOW);
  pinMode(PIN_OE, OUTPUT);  // важен порядок для analogWrite

  // === Установка яркости ===
  updateBrightness();

  // === Serial ===
  Serial.begin(SERIAL_BAUD);
  delay(1000);
  Serial.println("=== УСТРОЙСТВО ГОТОВО ===");
  Serial.println("Ожидание стартовой анимации...");

  // === IR ===
  irrecv.enableIRIn();

  // === Стартовая анимация ===
  startAnimation();

  Serial.println("Анимация завершена. Ожидание данных...");
  Serial.println("Формат: 0xFF,0xFF,...,0xFF,0x01");

}

void loop() {
  unsigned long now = millis();

  // === 1. Основные данные ===
  if (now - lastUpdate >= 1000) {
    lastUpdate = now;

    float tempC = calculateTemperature(analogRead(PIN_NTC));
    float voltageButtons = (analogRead(PIN_BUTTONS) / 1023.0) * V_REF;

    Serial.print("T:");
    Serial.print(tempC, 2);
    Serial.print("C V:");
    Serial.print(voltageButtons, 3);
    Serial.print("V");

    if (irrecv.decode(&results)) {
      Serial.print(" | IR:");
      printIRResult(&results);
      irrecv.resume();
    }
    Serial.println();
  }

  // === 2. Обработка Serial ===
  parseSerialInput();

  // === 3. Отправка данных ===
  if (dataReady) {
    sendDataWithMode(displayData, currentMode);
    dataReady = false;
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

// === Отправка данных ===
void sendDataWithMode(byte data[10], int mode) {
  // Перед отправкой — выключаем (аналогично)
  analogWrite(PIN_OE, 255);  // полностью HIGH → выключено
  delayMicroseconds(100);

  int bitCount = 0;

  for (int b = 0; b < 10; b++) {
    for (int i = 7; i >= 0; i--) {
      bool bit = (data[b] >> i) & 1;
      updateShiftRegister(bit);
      bitCount++;
      delayMicroseconds(20000);

      bool shouldUpdate = false;
      if (mode == MODE_BIT) {
        shouldUpdate = true;
      } else if (mode == MODE_REGISTER && (bitCount % 16) == 0) {
        shouldUpdate = true;
      } else if (mode == MODE_ALL && bitCount == 80) {
        shouldUpdate = true;
      }

      if (shouldUpdate) {
        pulseLatch();
        // После Latch — возвращаем ШИМ
        analogWrite(PIN_OE, 255 - pwmValue);  // включаем с регулировкой яркости
      }
    }
  }
}

// === Обновление одного бита ===
void updateShiftRegister(bool bit) {
  digitalWrite(PIN_SDI, bit);
  delayMicroseconds(5);
  digitalWrite(PIN_CLK, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_CLK, LOW);
  delayMicroseconds(5);
}

// === Latch ===
void pulseLatch() {
  digitalWrite(PIN_LATCH, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_LATCH, LOW);
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
void printIRResult(decode_results *res) {
  switch (res->decode_type) {
    case NEC:      Serial.print("NEC:0x"); break;
    case SONY:     Serial.print("SONY:0x"); break;
    case RC5:      Serial.print("RC5:0x"); break;
    case RC6:      Serial.print("RC6:0x"); break;
    case PANASONIC: Serial.print("PANASONIC:0x"); break;
    case SAMSUNG:  Serial.print("SAMSUNG:0x"); break;
    default:       Serial.print("UNK:0x"); break;
  }
  Serial.print(res->value, HEX);
}

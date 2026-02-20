#include <Arduino.h>

// ======================= Конфиг пинов =========================

// Таблица Digital (ID = 68)
const uint8_t DIGITAL_COUNT = 8;
const uint8_t digitalPins[DIGITAL_COUNT] = {
  22, 24, 26, 28, 30, 32, 34, 36
};

// Таблица PWM (ID = 80)
const uint8_t PWM_COUNT = 3;
const uint8_t pwmPins[PWM_COUNT] = {
  2, 3, 5
};

// Состояния (hardware)
bool    digitalState[DIGITAL_COUNT] = { false };
uint8_t pwmState[PWM_COUNT]        = { 0 };

// Инверсия для каждого канала
// false = обычная логика, true = инверсный канал
bool invertDigital[DIGITAL_COUNT] = {
  true, true, true, true,
  true, true, true, true
};

bool invertPWM[PWM_COUNT] = {
  false, false, false
};

// Логические состояния (с учётом инверсий)
bool    logicDigital[DIGITAL_COUNT] = { false };
uint8_t logicPWM[PWM_COUNT]         = { 0 };

const int TABLE_DIGITAL = 68; // 'D'
const int TABLE_PWM     = 80; // 'P'

// ======================= Error bits ==========================
//
// feedback bit layout: KXXXX87654321 (13 bits)
//
// bit0  -> ERROR_SYNTAX
// bit1  -> ERROR_1L_NO_DATA
// bit2  -> ERROR_1L_TOO_MANY_DATA
// bit3  -> ERROR_INVALID_CRC
// bit4  -> ERROR_NULL_CRC
// bit5  -> ERROR_2L_NO_DATA_PACKETS (warning)
// bit6  -> ERROR_2L_TOO_MANY_PACKETS
// bit7  -> ERROR_3L_WRONG_DATA_PACKETS
// bit8-11 -> PACKETS_COUNT (0..15)
// bit12 -> GET_KEYWORD

const uint16_t ERROR_NONE                   = 0;
const uint16_t ERROR_SYNTAX                 = 1 << 0;
const uint16_t ERROR_1L_NO_DATA             = 1 << 1;
const uint16_t ERROR_1L_TOO_MANY_DATA       = 1 << 2;
const uint16_t ERROR_INVALID_CRC            = 1 << 3;
const uint16_t ERROR_NULL_CRC               = 1 << 4;
const uint16_t ERROR_2L_NO_DATA_PACKETS     = 1 << 5;
const uint16_t ERROR_2L_TOO_MANY_PACKETS    = 1 << 6;
const uint16_t ERROR_3L_WRONG_DATA_PACKETS  = 1 << 7;
// PACKETS_COUNT = 0x0F << 8;
const uint16_t PACKETS_COUNT_MASK           = 0x0F << 8;
const uint16_t GET_KEYWORD                  = 1 << 12;

#define HAS_ERROR(mask, flag) (((mask) & (flag)) != 0)

// ======================= CRC8 (poly 0x07) ====================

uint8_t crc8_compute(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  const uint8_t poly = 0x07;

  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ poly;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// parse last 2 hex chars as CRC8
bool parseCRC8(const String& crcStr, uint8_t& outCrc) {
  String s = crcStr;
  s.trim();
  if (s.length() == 0) return false;

  // Берём последние 2 hex-символа
  if (s.length() > 2) {
    s = s.substring(s.length() - 2);
  }

  int value = 0;
  for (int i = 0; i < s.length(); ++i) {
    char c = s[i];
    int v;
    if (c >= '0' && c <= '9')      v = c - '0';
    else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
    else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
    else return false;
    value = (value << 4) | v;
  }
  outCrc = (uint8_t)(value & 0xFF);
  return true;
}

// ======================= Utils ===============================

void updateLogicStates() {
  // Логика (c учётом per-channel inversion)
  for (uint8_t i = 0; i < DIGITAL_COUNT; i++) {
    logicDigital[i] = invertDigital[i]
                      ? !digitalState[i]
                      :  digitalState[i];
  }

  for (uint8_t i = 0; i < PWM_COUNT; i++) {
    logicPWM[i] = invertPWM[i]
                  ? (uint8_t)(255 - pwmState[i])
                  : pwmState[i];
  }
}

void pinsInit() {
  for (uint8_t i = 0; i < DIGITAL_COUNT; ++i) {
    pinMode(digitalPins[i], OUTPUT);
    digitalWrite(digitalPins[i], digitalState[i] ^ invertDigital[i] ? HIGH : LOW);

  }
  for (uint8_t i = 0; i < PWM_COUNT; ++i) {
    pinMode(pwmPins[i], OUTPUT);
    analogWrite(pwmPins[i], 0);
    pwmState[i] = 0;
  }

  // Пример конфигурации инверсий (если нужно):
  // invertDigital[0] = true; // digital 0 = инверсный канал
  // invertPWM[2]     = true; // PWM2 работает в реверсе

  updateLogicStates();
}

// печать 13-битной маски KXXXX87654321
void printFeedbackBits(uint16_t fb) {
  char buf[14];
  for (int i = 12; i >= 0; --i) {
    buf[12 - i] = (fb & (1 << i)) ? '1' : '0';
  }
  buf[13] = '\0';
  Serial.println(buf);
}

// отправить строку с CRC8: "data/CRC"
void sendWithCRC(const String& data) {
  uint8_t crc = crc8_compute((const uint8_t*)data.c_str(), data.length());
  char crcHex[3];
  snprintf(crcHex, sizeof(crcHex), "%02X", crc);
  String out = data;
  out += "/";
  out += crcHex;
  Serial.println(out);
}

// ======================= State builders ======================

// showall отдаёт H/W состояния (без инверсии)
String buildShowAll() {
  String s;

  // Digital (hardware)
  for (uint8_t i = 0; i < DIGITAL_COUNT; ++i) {
    if (i > 0) s += ';';
    s += String(TABLE_DIGITAL);
    s += ",";
    s += String(i);
    s += ",";
    s += digitalState[i] ? "1" : "0";
  }

  // PWM (hardware)
  for (uint8_t i = 0; i < PWM_COUNT; ++i) {
    s += ';';
    s += String(TABLE_PWM);
    s += ",";
    s += String(i);
    s += ",";
    s += String(pwmState[i]);
  }

  return s;
}

String buildShowDigitalIndex(uint8_t idx) {
  if (idx >= DIGITAL_COUNT) {
    return String("ERR,BAD_INDEX");
  }
  String s;
  s += String(TABLE_DIGITAL);
  s += ",";
  s += String(idx);
  s += ",";
  s += digitalState[idx] ? "1" : "0";
  return s;
}

String buildShowAllPWM() {
  String s;
  for (uint8_t i = 0; i < PWM_COUNT; ++i) {
    if (i > 0) s += ';';
    s += String(TABLE_PWM);
    s += ",";
    s += String(i);
    s += ",";
    s += String(pwmState[i]);
  }
  return s;
}

// ======================= setAll режим ========================

bool g_setAllNextFrameIsFull = false;

// применить список команд (как в обычных data-пакетах: 68,0,1;80,2,255)
void applyStateList(const String& data, uint16_t& fb);

// ======================= Keyword handler =====================

uint16_t handleKeyword(const String& data) {
  uint16_t fb = GET_KEYWORD;

  // Разбираем keyword + аргументы: cmd,arg1,arg2,...
  int firstComma = data.indexOf(',');
  String cmd = (firstComma == -1) ? data : data.substring(0, firstComma);
  cmd.trim();

  if (cmd.equalsIgnoreCase("inited")) {
    // Просто сказать "ok"
    sendWithCRC("ok");
    return fb;
  }

  if (cmd.equalsIgnoreCase("showall")) {
    if (firstComma == -1) {
      // showall → вернуть всё
      String payload = buildShowAll();
      sendWithCRC(payload);
      return fb;
    }

    String rest = data.substring(firstComma + 1);
    rest.trim();

    int secondComma = rest.indexOf(',');
    String a = (secondComma == -1) ? rest : rest.substring(0, secondComma);
    a.trim();

    if (a == "68") {
      // showall,68,idx
      if (secondComma == -1) {
        fb |= ERROR_SYNTAX;
        return fb;
      }
      String b = rest.substring(secondComma + 1);
      b.trim();
      int idx = b.toInt();
      String payload = buildShowDigitalIndex((uint8_t)idx);
      sendWithCRC(payload);
      return fb;
    } else if (a == "80") {
      // showall,80,all или showall,80,idx
      if (secondComma == -1) {
        fb |= ERROR_SYNTAX;
        return fb;
      }
      String b = rest.substring(secondComma + 1);
      b.trim();
      if (b.equalsIgnoreCase("all")) {
        String payload = buildShowAllPWM();
        sendWithCRC(payload);
        return fb;
      } else {
        int idx = b.toInt();
        if (idx < 0 || idx >= PWM_COUNT) {
          sendWithCRC("ERR,BAD_INDEX");
          fb |= ERROR_SYNTAX;
          return fb;
        }
        String payload;
        payload += String(TABLE_PWM);
        payload += ",";
        payload += String(idx);
        payload += ",";
        payload += String(pwmState[idx]);
        sendWithCRC(payload);
        return fb;
      }
    } else {
      fb |= ERROR_SYNTAX;
      return fb;
    }
  }

  if (cmd.equalsIgnoreCase("setAll")) {
    // Следующий валидный кадр с Data будет трактоваться как полный список состояний
    g_setAllNextFrameIsFull = true;
    sendWithCRC("setAll_wait");
    return fb;
  }

  if (cmd.equalsIgnoreCase("end")) {
    g_setAllNextFrameIsFull = false;
    sendWithCRC("setAll_end");
    return fb;
  }

  fb |= ERROR_SYNTAX;
  return fb;
}

// ======================= Packets handler =====================

void applyStateList(const String& data, uint16_t& fb) {
  String tmp = data;
  tmp.trim();
  if (tmp.length() == 0) {
    fb |= ERROR_2L_NO_DATA_PACKETS;
    return;
  }

  String packets[8];
  int packetCount = 0;

  int start = 0;
  while (start < tmp.length() && packetCount < 8) {
    int sep = tmp.indexOf(';', start);
    if (sep == -1) sep = tmp.length();
    String p = tmp.substring(start, sep);
    p.trim();
    if (p.length() > 0) {
      packets[packetCount++] = p;
    }
    start = sep + 1;
  }

  if (packetCount == 0) {
    fb |= ERROR_2L_NO_DATA_PACKETS;
    return;
  }
  if (start < tmp.length()) {
    fb |= ERROR_2L_TOO_MANY_PACKETS;
  }

  uint16_t count4 = (uint16_t)(packetCount & 0x0F);
  fb |= (count4 << 8);

  for (int i = 0; i < packetCount; ++i) {
    String p = packets[i];
    int c1 = p.indexOf(',');
    int c2 = (c1 == -1) ? -1 : p.indexOf(',', c1 + 1);

    if (c1 == -1 || c2 == -1) {
      fb |= ERROR_3L_WRONG_DATA_PACKETS;
      continue;
    }

    String sTable = p.substring(0, c1); sTable.trim();
    String sIdx   = p.substring(c1 + 1, c2); sIdx.trim();
    String sVal   = p.substring(c2 + 1); sVal.trim();

    int tableId = sTable.toInt();
    int idx     = sIdx.toInt();
    int val     = sVal.toInt();

    if (tableId == TABLE_DIGITAL) {
      if (idx < 0 || idx >= DIGITAL_COUNT) {
        fb |= ERROR_3L_WRONG_DATA_PACKETS;
        continue;
      }

      // логическое значение из команды
      bool logicVal = (val != 0);

      // учитываем per-channel inversion
      if (invertDigital[idx]) {
        logicVal = !logicVal;
      }

      digitalState[idx] = logicVal;
      digitalWrite(digitalPins[idx], logicVal ? HIGH : LOW);

    } else if (tableId == TABLE_PWM) {
      if (idx < 0 || idx >= PWM_COUNT) {
        fb |= ERROR_3L_WRONG_DATA_PACKETS;
        continue;
      }

      int inputVal = val;
      if (inputVal < 0)   inputVal = 0;
      if (inputVal > 255) inputVal = 255;

      uint8_t logicPwm = (uint8_t)inputVal;

      // учитываем per-channel inversion
      if (invertPWM[idx]) {
        logicPwm = (uint8_t)(255 - logicPwm);
      }

      pwmState[idx] = logicPwm;
      analogWrite(pwmPins[idx], pwmState[idx]);

    } else {
      fb |= ERROR_3L_WRONG_DATA_PACKETS;
      continue;
    }
  }

  // после всех изменений обновляем логические таблицы
  updateLogicStates();
}

uint16_t handlePackets(const String& data) {
  uint16_t fb = ERROR_NONE;
  applyStateList(data, fb);
  return fb;
}

// ======================= Top-level parser ====================

void processLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  uint16_t feedback = ERROR_NONE;

  // 1 уровень: Data/CRC
  int firstSlash = line.indexOf('/');
  int lastSlash  = line.lastIndexOf('/');

  if (firstSlash == -1) {
    feedback |= ERROR_SYNTAX;
    printFeedbackBits(feedback);
    return;
  }
  if (firstSlash != lastSlash) {
    feedback |= ERROR_1L_TOO_MANY_DATA;
    printFeedbackBits(feedback);
    return;
  }

  String dataPart = line.substring(0, firstSlash);
  String crcPart  = line.substring(firstSlash + 1);
  dataPart.trim();
  crcPart.trim();

  if (dataPart.length() == 0) {
    feedback |= ERROR_1L_NO_DATA;
    printFeedbackBits(feedback);
    return;
  }
  if (crcPart.length() == 0) {
    feedback |= ERROR_NULL_CRC;
    printFeedbackBits(feedback);
    return;
  }

  // CRC8 check
  uint8_t crcRemote;
  if (!parseCRC8(crcPart, crcRemote)) {
    feedback |= ERROR_INVALID_CRC;
    printFeedbackBits(feedback);
    return;
  }

  uint8_t crcLocal = crc8_compute((const uint8_t*)dataPart.c_str(), dataPart.length());
  if (crcLocal != crcRemote) {
    feedback |= ERROR_INVALID_CRC;
    printFeedbackBits(feedback);
    return;
  }

  // Keyword или пакеты
  bool isKeyword = false;
  {
    String firstToken;
    int comma = dataPart.indexOf(',');
    if (comma == -1) firstToken = dataPart;
    else firstToken = dataPart.substring(0, comma);
    firstToken.trim();
    if (firstToken.length() > 0 && !isDigit(firstToken[0])) {
      isKeyword = true;
    }
  }

  if (isKeyword) {
    feedback |= handleKeyword(dataPart);
  } else {
    if (g_setAllNextFrameIsFull) {
      applyStateList(dataPart, feedback);
      g_setAllNextFrameIsFull = false;
    } else {
      feedback |= handlePackets(dataPart);
    }
  }

  // Отправляем feedback как KXXXX87654321
  printFeedbackBits(feedback);
}

// ======================= setup/loop ==========================

void setup() {
  Serial.begin(115200);
  pinsInit();
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  Serial.println(F("deviceControlModule (CRC8, Digital+PWM, per-channel invert) started"));
}

void loop() {
  static String line;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      processLine(line);
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
}

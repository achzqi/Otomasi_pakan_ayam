// ============================================================
//  SMART POULTRY FEEDER - ESP32 DevKit V1
//  Version : 3.0.0
//  Fix     :
//    - feedingAction: display "sudah/target" tidak lagi 0/Xg
//    - bacaBerat: median filter anti-noise vibrasi motor
//    - LCD tidak mati setelah feed error / early return
//    - Break condition lebih robust (hysteresis + multi-sample)
//    - lcdForceUpdate() untuk paksa refresh setelah error
//    - Semua early return di feedingAction kini restore LCD
//
//  LIBRARIES REQUIRED (Library Manager):
//    - LiquidCrystal_I2C  (Frank de Brabander)
//    - DHT sensor library  (Adafruit)
//    - HX711               (bogde)
// ============================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <HX711.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

// ============================================================
//  PIN DEFINITIONS
// ============================================================
#define DHT_PIN     16
#define HX711_DT    32
#define HX711_SCK   33
#define MOTOR_IN1   26
#define MOTOR_IN2   27
#define MQ135_PIN   34

// ============================================================
//  I2C ADDRESSES
// ============================================================
#define LCD_ADDR    0x27
#define KEYPAD_ADDR 0x21

// ============================================================
//  CONSTANTS
// ============================================================
#define DHT_TYPE        DHT11
#define MOTOR_TIMEOUT   15000UL   // ms timeout motor
#define NOISE_THRESHOLD 1.5f      // gram, noise floor
#define CALIBRATION     225.0f
#define LCD_REFRESH_MS  400

// Berapa kali berturut-turut berat harus di bawah target sebelum motor stop
// (anti false-stop karena spike noise vibrasi motor)
#define CONFIRM_SAMPLES 3

// ============================================================
//  WIFI CREDENTIALS
// ============================================================
const char* ssid     = "sapi ganteng.com";
const char* password = "#12345678";

// ============================================================
//  KEYPAD MAPPING
// ============================================================
const char keypadMap[4][4] = {
  {'D','#','0','*'},
  {'C','9','8','7'},
  {'B','6','5','4'},
  {'A','3','2','1'}
};

// ============================================================
//  OBJECTS
// ============================================================
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
DHT               dht(DHT_PIN, DHT_TYPE);
HX711             scale;
Preferences       prefs;

// ============================================================
//  JADWAL STRUCT
// ============================================================
struct Jadwal {
  uint8_t jam;
  uint8_t menit;
  int     gram;
};

Jadwal jadwal1, jadwal2;

// ============================================================
//  ANTI DOUBLE FEED
// ============================================================
int  lastFeedDay  = -1;
int  j1LastMinute = -1;
int  j2LastMinute = -1;
bool j1Done       = false;
bool j2Done       = false;

// ============================================================
//  LCD STATE  (anti flicker)
// ============================================================
String lcdRow0  = "", lcdRow1  = "";
String prevRow0 = "~~~~~~~~~~~~~~~~"; // intentionally invalid → force first write
String prevRow1 = "~~~~~~~~~~~~~~~~";
unsigned long lcdLastUpdate = 0;

// ============================================================
//  KEYPAD STATE
// ============================================================
char          kp_lastKey     = '\0';
unsigned long kp_pressTime   = 0;
bool          kp_waitRelease = false;
#define KP_DEBOUNCE_MS  80

// ============================================================
//  FORWARD DECLARATIONS
// ============================================================
char    bacaKeypad();
char    bacaKeypadRaw();
char    bacaKeypadBlocking();
void    tampilHome();
void    setJadwal(uint8_t slot);
void    monitoring();
void    manualFeed();
void    feedingAction(int gramTarget, const char* label);
void    cekJadwal();
void    saveData();
void    loadData();
void    motorOn();
void    motorOff();
float   bacaBerat();
float   bacaBeratStabil(uint8_t samples);
bool    isHX711OK();
void    lcdSet(const char* r0, const char* r1);
void    lcdFlush();
void    lcdForceUpdate();   // paksa tulis ulang tanpa cek prev
String  padRight(String s, int len);
void    connectWiFi();
void    syncNTP();
void    resetHarianJikaPerlu();

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  motorOff();

  Wire.begin(21, 22);
  Wire.setClock(100000);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Booting v3.0...");

  dht.begin();

  scale.begin(HX711_DT, HX711_SCK);
  delay(500);
  scale.set_scale(CALIBRATION);
  scale.set_offset(188584);

  loadData();
  connectWiFi();
  syncNTP();

  // Paksa layar home tampil bersih
  lcdForceUpdate();
  Serial.println("[BOOT] Ready.");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  resetHarianJikaPerlu();
  cekJadwal();

  char key = bacaKeypad();

  tampilHome();

  if      (key == 'A') setJadwal(1);
  else if (key == 'B') setJadwal(2);
  else if (key == 'C') monitoring();
  else if (key == 'D') manualFeed();

  delay(20);
}

// ============================================================
//  BACA KEYPAD RAW
// ============================================================
char bacaKeypadRaw() {
  for (int row = 0; row < 4; row++) {
    uint8_t rowMask = (~(1 << row) & 0x0F) | 0xF0;

    Wire.beginTransmission(KEYPAD_ADDR);
    Wire.write(rowMask);
    if (Wire.endTransmission() != 0) continue;

    delayMicroseconds(150);

    Wire.requestFrom((uint8_t)KEYPAD_ADDR, (uint8_t)1);
    if (!Wire.available()) continue;
    uint8_t data = Wire.read();

    uint8_t colBits = (data >> 4) & 0x0F;
    for (int col = 0; col < 4; col++) {
      if (!(colBits & (1 << col))) {
        return keypadMap[row][col];
      }
    }
  }

  Wire.beginTransmission(KEYPAD_ADDR);
  Wire.write(0xFF);
  Wire.endTransmission();

  return '\0';
}

// ============================================================
//  BACA KEYPAD (debounce non-blocking)
// ============================================================
char bacaKeypad() {
  char raw = bacaKeypadRaw();

  if (raw != '\0') {
    if (!kp_waitRelease) {
      unsigned long now = millis();
      if (raw != kp_lastKey) {
        kp_lastKey   = raw;
        kp_pressTime = now;
      }
      if (millis() - kp_pressTime >= KP_DEBOUNCE_MS) {
        kp_waitRelease = true;
        Serial.printf("[KEY] %c\n", raw);
        return raw;
      }
    }
  } else {
    kp_lastKey     = '\0';
    kp_pressTime   = 0;
    kp_waitRelease = false;
  }
  return '\0';
}

// ============================================================
//  BACA KEYPAD BLOCKING (hanya di dalam menu input)
// ============================================================
char bacaKeypadBlocking() {
  kp_waitRelease = false;
  kp_lastKey     = '\0';
  while (true) {
    char k = bacaKeypad();
    if (k != '\0') return k;
    delay(10);
  }
}

// ============================================================
//  HOME SCREEN
// ============================================================
void tampilHome() {
  unsigned long now = millis();
  if (now - lcdLastUpdate < LCD_REFRESH_MS) return;
  lcdLastUpdate = now;

  struct tm ti;
  char timeBuf[17];
  if (getLocalTime(&ti)) {
    snprintf(timeBuf, sizeof(timeBuf), "    %02d:%02d:%02d    ",
             ti.tm_hour, ti.tm_min, ti.tm_sec);
  } else {
    strncpy(timeBuf, "  --:--:--      ", sizeof(timeBuf));
  }

  char schedBuf[17];
  snprintf(schedBuf, sizeof(schedBuf), "%02d:%02d    %02d:%02d",
           jadwal1.jam, jadwal1.menit, jadwal2.jam, jadwal2.menit);

  lcdSet(timeBuf, schedBuf);
  lcdFlush();
}

// ============================================================
//  SET JADWAL
// ============================================================
void setJadwal(uint8_t slot) {
  const char* label = (slot == 1) ? "Set J1" : "Set J2";

  lcdSet(label, "HHMM:");
  lcdForceUpdate();

  String inputHHMM = "";
  while (true) {
    char k = bacaKeypadBlocking();
    if (k >= '0' && k <= '9') {
      if (inputHHMM.length() < 4) {
        inputHHMM += k;
        String d = "HHMM:" + inputHHMM;
        lcdSet(label, d.c_str());
        lcdFlush();
      }
    } else if (k == '*') {
      if (inputHHMM.length() > 0) {
        inputHHMM.remove(inputHHMM.length() - 1);
        String d = "HHMM:" + inputHHMM;
        lcdSet(label, d.c_str());
        lcdFlush();
      }
    } else if (k == '#') {
      if (inputHHMM.length() == 4) break;
    }
  }

  uint8_t jam   = inputHHMM.substring(0, 2).toInt();
  uint8_t menit = inputHHMM.substring(2, 4).toInt();
  if (jam   > 23) jam   = 23;
  if (menit > 59) menit = 59;

  lcdSet("Gram?", "");
  lcdForceUpdate();

  String inputGram = "";
  while (true) {
    char k = bacaKeypadBlocking();
    if (k >= '0' && k <= '9') {
      if (inputGram.length() < 5) {
        inputGram += k;
        lcdSet("Gram?", inputGram.c_str());
        lcdFlush();
      }
    } else if (k == '*') {
      if (inputGram.length() > 0) {
        inputGram.remove(inputGram.length() - 1);
        lcdSet("Gram?", inputGram.c_str());
        lcdFlush();
      }
    } else if (k == '#') {
      if (inputGram.length() > 0) break;
    }
  }

  int gram = inputGram.toInt();

  if (slot == 1) {
    jadwal1 = {jam, menit, gram};
    j1Done = false; j1LastMinute = -1;
  } else {
    jadwal2 = {jam, menit, gram};
    j2Done = false; j2LastMinute = -1;
  }
  saveData();

  lcdSet("Saved!", "");
  lcdForceUpdate();
  delay(1500);
  lcdForceUpdate(); // bersihkan sebelum kembali ke home
}

// ============================================================
//  MONITORING
// ============================================================
void monitoring() {
  char r0[17], r1[17];

  float suhu  = dht.readTemperature();
  float humid = dht.readHumidity();
  if (isnan(suhu) || isnan(humid)) {
    lcdSet("DHT ERROR", "Check sensor");
  } else {
    snprintf(r0, sizeof(r0), "Suhu: %.1f C", suhu);
    snprintf(r1, sizeof(r1), "Humid: %.0f%%", humid);
    lcdSet(r0, r1);
  }
  lcdForceUpdate();
  delay(3000);

  float berat = bacaBeratStabil(5);
  snprintf(r0, sizeof(r0), "Pakan:");
  snprintf(r1, sizeof(r1), "%.0f gram", berat < 0 ? 0 : berat);
  lcdSet(r0, r1);
  lcdForceUpdate();
  delay(3000);

  int gasVal = analogRead(MQ135_PIN);
  const char* kondisi = (gasVal <= 300) ? "AMAN" :
                        (gasVal <= 600) ? "WASPADA" : "BURUK";
  snprintf(r0, sizeof(r0), "Gas: %s", kondisi);
  snprintf(r1, sizeof(r1), "ADC: %d", gasVal);
  lcdSet(r0, r1);
  lcdForceUpdate();
  delay(3000);

  lcdForceUpdate(); // restore agar home screen bisa tulis ulang
}

// ============================================================
//  MANUAL FEED
// ============================================================
void manualFeed() {
  lcdSet("Feed Manual", "Gram: ");
  lcdForceUpdate();

  String inputGram = "";
  while (true) {
    char k = bacaKeypadBlocking();
    if (k >= '0' && k <= '9') {
      if (inputGram.length() < 5) {
        inputGram += k;
        String disp = "Gram: " + inputGram;
        lcdSet("Feed Manual", disp.c_str());
        lcdFlush();
      }
    } else if (k == '*') {
      if (inputGram.length() > 0) {
        inputGram.remove(inputGram.length() - 1);
        String disp = "Gram: " + inputGram;
        lcdSet("Feed Manual", disp.c_str());
        lcdFlush();
      }
    } else if (k == '#') {
      if (inputGram.length() > 0) break;
    }
  }

  feedingAction(inputGram.toInt(), "MANUAL");
}

// ============================================================
//  FEEDING ACTION  — versi fix
//
//  BUG LAMA yang diperbaiki:
//  1. "0/Xg" di LCD → karena sudah = beratAwal - beratSkrg bisa
//     negatif saat motor baru jalan (vibrasi + filter belum stabil).
//     FIX: pakai max(0, sudah) DAN ambil berat awal dari rata-rata
//          3 sample stabil sebelum motor hidup.
//
//  2. Break condition salah → FIX: butuh CONFIRM_SAMPLES kali
//     berturut-turut berat di bawah target (anti noise spike).
//
//  3. LCD mati setelah error/early-return → FIX: setiap jalur
//     keluar fungsi ini wajib panggil lcdForceUpdate() di akhir.
// ============================================================
void feedingAction(int gramTarget, const char* label) {
  Serial.printf("[FEED] %s – target=%dg\n", label, gramTarget);

  // --- Guard: HX711 siap? ---
  if (!isHX711OK()) {
    lcdSet("HX711 ERROR", "Cek sensor!");
    lcdForceUpdate();
    motorOff();
    delay(2000);
    lcdForceUpdate();
    return;
  }

  // --- Ambil berat awal yang stabil (sebelum motor nyala) ---
  // Gunakan 5 sample rata-rata bukan 1 sample raw
  float beratAwal = bacaBeratStabil(5);
  if (beratAwal < 0) beratAwal = 0;

  float targetAkhir = beratAwal - (float)gramTarget;
  Serial.printf("[FEED] beratAwal=%.1fg targetAkhir=%.1fg\n", beratAwal, targetAkhir);

  // --- Stok kurang? ---
  if (beratAwal < (float)gramTarget + NOISE_THRESHOLD) {
    char r1[17];
    snprintf(r1, sizeof(r1), "Stok: %.0fg", beratAwal < 0 ? 0 : beratAwal);
    lcdSet("Stok Kurang!", r1);
    lcdForceUpdate();
    Serial.println("[FEED] Stok kurang.");
    delay(3000);
    lcdForceUpdate();
    return;
  }

  // --- Tampil status sebelum motor nyala ---
  lcdSet("FEEDING...", "Menghitung...");
  lcdForceUpdate();

  // --- Nyalakan motor ---
  motorOn();

  unsigned long tStart      = millis();
  unsigned long lastDisplay = millis(); // set ke now → update pertama 500ms setelah motor on
  uint8_t       confirmCount = 0;   // counter konfirmasi berhenti

  while (true) {

    // Baca berat SEKALI per iterasi (cepat, 1 sample)
    float beratSkrg = bacaBerat();

    // Hitung sudah keluar; clamp ke [0, gramTarget]
    float sudah = beratAwal - beratSkrg;
    if (sudah < 0)           sudah = 0;
    if (sudah > gramTarget)  sudah = (float)gramTarget;

    // ── Kondisi berhenti: berat sudah turun ke/di bawah target ──
    // Pakai hysteresis kecil (0.5g) + konfirmasi CONFIRM_SAMPLES kali
    // untuk menghindari false stop akibat vibrasi motor.
    if (beratSkrg <= (targetAkhir + 0.5f)) {
      confirmCount++;
      if (confirmCount >= CONFIRM_SAMPLES) break;
    } else {
      confirmCount = 0; // reset jika berat naik lagi (bounce)
    }

    // ── Timeout ──
    if (millis() - tStart > MOTOR_TIMEOUT) {
      motorOff();
      lcdSet("Feed Timeout!", "Motor Stop");
      lcdForceUpdate();
      Serial.println("[FEED] TIMEOUT");
      delay(3000);
      lcdForceUpdate();
      return;
    }

    // ── Update LCD setiap 500ms ──
    if (millis() - lastDisplay >= 500) {
      lastDisplay = millis();
      char buf[17];
      snprintf(buf, sizeof(buf), "%.0f / %dg", sudah, gramTarget);
      lcdSet("FEEDING...", buf);
      lcdFlush(); // pakai flush biar tidak flicker jika sama
    }

    delay(60); // cek ~16x/detik
  }

  motorOff();

  // Tunggu sebentar agar timbangan stabil setelah motor mati
  delay(400);
  float sisa = bacaBeratStabil(5);
  if (sisa < 0) sisa = 0;

  float keluar = beratAwal - sisa;
  if (keluar < 0) keluar = 0;

  char doneR0[17], doneR1[17];
  snprintf(doneR0, sizeof(doneR0), "Keluar: %.0fg", keluar);
  snprintf(doneR1, sizeof(doneR1), "Sisa: %.0fg",   sisa);
  lcdSet(doneR0, doneR1);
  lcdForceUpdate();
  Serial.printf("[FEED] Done. Keluar=%.1fg Sisa=%.1fg\n", keluar, sisa);
  delay(3000);

  // Wajib force update agar home screen bisa tulis ulang bersih
  lcdForceUpdate();
}

// ============================================================
//  CEK JADWAL OTOMATIS
// ============================================================
void cekJadwal() {
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  int nowHour = ti.tm_hour;
  int nowMin  = ti.tm_min;
  int nowCode = nowHour * 100 + nowMin;

  if (!j1Done) {
    if (nowHour == jadwal1.jam && nowMin == jadwal1.menit && j1LastMinute != nowCode) {
      j1LastMinute = nowCode;
      j1Done       = true;
      saveData();
      Serial.printf("[AUTO] J1 trigger %02d:%02d\n", nowHour, nowMin);
      feedingAction(jadwal1.gram, "J1");
    }
  }

  if (!j2Done) {
    if (nowHour == jadwal2.jam && nowMin == jadwal2.menit && j2LastMinute != nowCode) {
      j2LastMinute = nowCode;
      j2Done       = true;
      saveData();
      Serial.printf("[AUTO] J2 trigger %02d:%02d\n", nowHour, nowMin);
      feedingAction(jadwal2.gram, "J2");
    }
  }
}

// ============================================================
//  RESET HARIAN
// ============================================================
void resetHarianJikaPerlu() {
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  int hariIni = ti.tm_wday;
  if (hariIni != lastFeedDay) {
    lastFeedDay  = hariIni;
    j1Done       = false;
    j2Done       = false;
    j1LastMinute = -1;
    j2LastMinute = -1;
    saveData();
    Serial.printf("[RESET] Hari baru %d – flag direset.\n", hariIni);
  }
}

// ============================================================
//  SAVE / LOAD
// ============================================================
void saveData() {
  prefs.begin("feeder", false);
  prefs.putUChar("j1h",  jadwal1.jam);
  prefs.putUChar("j1m",  jadwal1.menit);
  prefs.putInt  ("j1g",  jadwal1.gram);
  prefs.putUChar("j2h",  jadwal2.jam);
  prefs.putUChar("j2m",  jadwal2.menit);
  prefs.putInt  ("j2g",  jadwal2.gram);
  prefs.putInt  ("day",  lastFeedDay);
  prefs.putBool ("d1",   j1Done);
  prefs.putBool ("d2",   j2Done);
  prefs.putInt  ("j1lm", j1LastMinute);
  prefs.putInt  ("j2lm", j2LastMinute);
  prefs.end();
  Serial.println("[PREFS] Saved.");
}

void loadData() {
  prefs.begin("feeder", true);
  jadwal1.jam   = prefs.getUChar("j1h",  6);
  jadwal1.menit = prefs.getUChar("j1m",  0);
  jadwal1.gram  = prefs.getInt  ("j1g",  200);
  jadwal2.jam   = prefs.getUChar("j2h",  17);
  jadwal2.menit = prefs.getUChar("j2m",  0);
  jadwal2.gram  = prefs.getInt  ("j2g",  150);
  lastFeedDay   = prefs.getInt  ("day",  -1);
  j1Done        = prefs.getBool ("d1",   false);
  j2Done        = prefs.getBool ("d2",   false);
  j1LastMinute  = prefs.getInt  ("j1lm", -1);
  j2LastMinute  = prefs.getInt  ("j2lm", -1);
  prefs.end();
  Serial.printf("[PREFS] J1=%02d:%02d %dg | J2=%02d:%02d %dg | day=%d\n",
                jadwal1.jam, jadwal1.menit, jadwal1.gram,
                jadwal2.jam, jadwal2.menit, jadwal2.gram, lastFeedDay);
}

// ============================================================
//  MOTOR
// ============================================================
void motorOn() {
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  Serial.println("[MOTOR] ON");
}

void motorOff() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  Serial.println("[MOTOR] OFF");
}

// ============================================================
//  BACA BERAT — 1 sample + exponential moving average
//  Dipakai saat FEEDING (realtime, cepat)
// ============================================================
float bacaBerat() {
  if (!scale.is_ready()) return -1.0f;

  float g = scale.get_units(1);

  // EMA ringan: cepat tapi tetap halus
  static float filtered = -9999.0f;
  if (filtered < -9000.0f) {
    filtered = g; // init pertama kali
  } else {
    filtered = (filtered * 0.65f) + (g * 0.35f);
  }

  // Noise floor
  if (fabsf(filtered) < NOISE_THRESHOLD) filtered = 0.0f;

  return filtered;
}

// ============================================================
//  BACA BERAT STABIL — multi sample + buang outlier
//  Dipakai SEBELUM motor nyala dan SETELAH motor mati
//  untuk mendapat angka yang lebih akurat.
// ============================================================
float bacaBeratStabil(uint8_t samples) {
  if (!scale.is_ready()) return 0.0f;
  if (samples < 3) samples = 3;

  // Kumpulkan sample
  float buf[10];
  uint8_t n = (samples > 10) ? 10 : samples;
  for (uint8_t i = 0; i < n; i++) {
    buf[i] = scale.get_units(1);
    delay(30);
  }

  // Bubble sort kecil untuk buang outlier (median)
  for (uint8_t i = 0; i < n - 1; i++) {
    for (uint8_t j = 0; j < n - 1 - i; j++) {
      if (buf[j] > buf[j+1]) {
        float tmp = buf[j]; buf[j] = buf[j+1]; buf[j+1] = tmp;
      }
    }
  }

  // Ambil median (buang 1 tertinggi dan 1 terendah jika n >= 3)
  float sum = 0;
  uint8_t from = (n >= 3) ? 1 : 0;
  uint8_t to   = (n >= 3) ? n - 1 : n;
  for (uint8_t i = from; i < to; i++) sum += buf[i];
  float result = sum / (to - from);

  if (fabsf(result) < NOISE_THRESHOLD) result = 0.0f;
  return result;
}

bool isHX711OK() {
  return scale.is_ready();
}

// ============================================================
//  LCD HELPERS
// ============================================================
void lcdSet(const char* r0, const char* r1) {
  lcdRow0 = padRight(String(r0), 16);
  lcdRow1 = padRight(String(r1), 16);
}

// Tulis hanya baris yang berubah (anti flicker)
void lcdFlush() {
  if (lcdRow0 != prevRow0) {
    lcd.setCursor(0, 0);
    lcd.print(lcdRow0);
    prevRow0 = lcdRow0;
  }
  if (lcdRow1 != prevRow1) {
    lcd.setCursor(0, 1);
    lcd.print(lcdRow1);
    prevRow1 = lcdRow1;
  }
}

// Paksa tulis ulang tanpa peduli prev — pakai setelah error / menu selesai
// Ini yang mencegah LCD "mati" (tidak mau update) setelah feedingAction error.
void lcdForceUpdate() {
  // Invalidate cache agar lcdFlush pasti tulis
  prevRow0 = "~~~~~~~~~~~~~~~~";
  prevRow1 = "~~~~~~~~~~~~~~~~";
  lcdFlush();
}

String padRight(String s, int len) {
  while ((int)s.length() < len) s += ' ';
  if ((int)s.length() > len)    s = s.substring(0, len);
  return s;
}

// ============================================================
//  WIFI
// ============================================================
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s\n", ssid);
  lcdSet("Connecting WiFi", ssid);
  lcdFlush();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    lcdSet("WiFi OK", WiFi.localIP().toString().c_str());
    lcdFlush();
    delay(1000);
  } else {
    Serial.println("[WiFi] FAILED – no NTP.");
    lcdSet("WiFi FAILED", "No NTP sync");
    lcdFlush();
    delay(1500);
  }
}

// ============================================================
//  NTP SYNC
// ============================================================
void syncNTP() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("[NTP] Syncing...");
  delay(1000);

  configTime(8 * 3600, 0,
             "id.pool.ntp.org",
             "asia.pool.ntp.org",
             "time.google.com");

  struct tm ti;
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&ti)) {
      Serial.printf("[NTP] OK: %02d:%02d:%02d\n", ti.tm_hour, ti.tm_min, ti.tm_sec);
      return;
    }
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\n[NTP] FAILED.");
}

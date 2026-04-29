/*
 * ============================================================
 *  Motor Induksi Fault Detection — ESP32 + MPU6050
 *  Versi  : 2.0
 *  Sampling: 1 kHz  |  FFT: 256 titik
 * ------------------------------------------------------------
 *  OUTPUT UTAMA — Kode Kerusakan (int 0–5):
 *
 *    0 = Normal / Tidak ada kerusakan terdeteksi
 *    1 = Kerusakan Bantalan (Bearing Fault)
 *    2 = Ketidakseimbangan Rotor (Unbalance)
 *    3 = Misalignment Poros
 *    4 = Resonansi Struktur
 *    5 = Kelonggaran Mekanik (Mechanical Looseness)
 *
 *  Jika lebih dari satu kondisi terdeteksi, kode dengan
 *  amplitudo tertinggi (paling parah) yang dikembalikan.
 *
 * ------------------------------------------------------------
 *  Library (install via Arduino Library Manager):
 *    - Adafruit MPU6050
 *    - Adafruit BusIO
 *    - ArduinoFFT  (by kosme, versi 2.x)
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <arduinoFFT.h>

// ─────────────────────────────────────────────────────────────
//  Kode Kerusakan (konstanta bernama — lebih mudah dibaca)
// ─────────────────────────────────────────────────────────────
#define FAULT_NORMAL      0   // Motor sehat
#define FAULT_BEARING     1   // Kerusakan bantalan
#define FAULT_UNBALANCE   2   // Ketidakseimbangan rotor
#define FAULT_MISALIGN    3   // Misalignment poros
#define FAULT_RESONANCE   4   // Resonansi struktur
#define FAULT_LOOSENESS   5   // Kelonggaran mekanik

// ─────────────────────────────────────────────────────────────
//  Konfigurasi Hardware
// ─────────────────────────────────────────────────────────────
#define SDA_PIN          21
#define SCL_PIN          22
#define LED_PIN           2   // Built-in LED ESP32

// ─────────────────────────────────────────────────────────────
//  Konfigurasi Sampling & FFT
// ─────────────────────────────────────────────────────────────
#define SAMPLES           256       // Harus pangkat 2
#define SAMPLE_RATE       1000.0f   // Hz
#define SAMPLE_PERIOD_US  1000      // µs (= 1 ms)

// ─────────────────────────────────────────────────────────────
//  Threshold Getaran (ISO 10816-3, Group 2)
// ─────────────────────────────────────────────────────────────
#define THR_DANGER        0.70f     // g — zona bahaya

// ─────────────────────────────────────────────────────────────
//  Frekuensi Target (Hz)
//  Motor 2-pole, supply 50 Hz → ~3000 RPM → 1× = 50 Hz
//  Sesuaikan FREQ_1X dengan RPM aktual motor Anda: RPM/60
// ─────────────────────────────────────────────────────────────
#define FREQ_1X           50.0f    // Fundamental rotasi
#define FREQ_2X          100.0f    // Harmonik ke-2
#define FREQ_3X          150.0f    // Harmonik ke-3
#define FREQ_HALF_X       25.0f    // Sub-harmonik 0.5× (looseness)
#define FREQ_BPFO        162.0f    // Bearing Pass Freq Outer race
#define FREQ_BPFI        108.0f    // Bearing Pass Freq Inner race
#define FREQ_RESONANCE    28.0f    // Frekuensi natural struktur (estimasi)
#define FREQ_BAND          5.0f    // ± bandwidth pencarian (Hz)

// ─────────────────────────────────────────────────────────────
//  Threshold Amplitudo FFT
//  (setelah normalisasi N/2 — satuan: g)
// ─────────────────────────────────────────────────────────────
#define AMP_BEARING       0.15f
#define AMP_UNBALANCE     0.25f
#define AMP_MISALIGN      0.18f
#define AMP_RESONANCE     0.30f
#define AMP_LOOSENESS     0.12f

// ═════════════════════════════════════════════════════════════
//  Struktur Data
// ═════════════════════════════════════════════════════════════

struct Axes { float x, y, z; };

/* Skor tiap jenis kerusakan (amplitudo FFT tertinggi yg terdeteksi).
   Nilai 0.0 = tidak terdeteksi.  Digunakan untuk memilih kode
   kerusakan paling dominan saat lebih dari satu terdeteksi. */
struct FaultScores {
  float bearing;
  float unbalance;
  float misalign;
  float resonance;
  float looseness;
  float rmsX, rmsY, rmsZ;
};

// ═════════════════════════════════════════════════════════════
//  Buffer FFT Global (menghindari stack overflow)
// ═════════════════════════════════════════════════════════════
double vRealX[SAMPLES], vImagX[SAMPLES];
double vRealY[SAMPLES], vImagY[SAMPLES];
double vRealZ[SAMPLES], vImagZ[SAMPLES];

double magX[SAMPLES];   // Magnitudo setelah FFT — X
double magY[SAMPLES];   // Magnitudo setelah FFT — Y
double magZ[SAMPLES];   // Magnitudo setelah FFT — Z

double tmpR[SAMPLES], tmpI[SAMPLES];  // Buffer kerja FFT

Adafruit_MPU6050 mpu;
ArduinoFFT<double> FFT;

unsigned long lastSampleTime = 0;
int  bufIdx  = 0;
bool bufFull = false;

// Simpan kode kerusakan terakhir untuk akses dari luar fungsi
int  lastFaultCode = FAULT_NORMAL;

// ═════════════════════════════════════════════════════════════
//  Fungsi Utilitas FFT
// ═════════════════════════════════════════════════════════════

/* Hitung RMS dari array double */
float calcRMS(const double* buf, int len) {
  double sum = 0.0;
  for (int i = 0; i < len; i++) sum += buf[i] * buf[i];
  return (float)sqrt(sum / len);
}

/* Cari amplitudo puncak pada band frekuensi tertentu.
   Mengembalikan amplitudo terbesar (g) dalam band [target ± band]. */
float peakInBand(const double* mag, float targetHz, float bandHz) {
  float res  = SAMPLE_RATE / SAMPLES;               // Hz/bin
  int lo = max(1,              (int)((targetHz - bandHz) / res));
  int hi = min(SAMPLES / 2 - 1, (int)((targetHz + bandHz) / res));
  double peak = 0.0;
  for (int i = lo; i <= hi; i++) {
    if (mag[i] > peak) peak = mag[i];
  }
  return (float)peak;
}

/* Jalankan FFT pada satu sumbu dan isi array magnitudo.
   Sumber: vReal* yang sudah terisi 256 sampel. */
void runFFT(const double* vReal, double* outMag) {
  // Salin ke buffer kerja
  memcpy(tmpR, vReal, SAMPLES * sizeof(double));
  memset(tmpI, 0,     SAMPLES * sizeof(double));

  // Hann window → FFT → magnitudo
  FFT.windowing(tmpR, SAMPLES, FFT_WIN_TYP_HANN, FFT_FORWARD);
  FFT.compute(tmpR, tmpI, SAMPLES, FFT_FORWARD);
  FFT.complexToMagnitude(tmpR, tmpI, SAMPLES);

  // Normalisasi N/2
  float norm = SAMPLES / 2.0f;
  for (int i = 0; i < SAMPLES; i++) outMag[i] = tmpR[i] / norm;
}

// ═════════════════════════════════════════════════════════════
//  Hitung Skor Semua Kerusakan
// ═════════════════════════════════════════════════════════════
FaultScores calcScores() {
  FaultScores s = {};

  // ── Jalankan FFT tiga sumbu ──────────────────────────────
  runFFT(vRealX, magX);
  runFFT(vRealY, magY);
  runFFT(vRealZ, magZ);

  // ── RMS ─────────────────────────────────────────────────
  s.rmsX = calcRMS(vRealX, SAMPLES);
  s.rmsY = calcRMS(vRealY, SAMPLES);
  s.rmsZ = calcRMS(vRealZ, SAMPLES);

  // ── 1. BEARING (Kode 1) ──────────────────────────────────
  //  Ciri: puncak di BPFO dan/atau BPFI pada sumbu radial
  float bpfo_x = peakInBand(magX, FREQ_BPFO, FREQ_BAND * 2);
  float bpfi_x = peakInBand(magX, FREQ_BPFI, FREQ_BAND * 2);
  float bpfo_y = peakInBand(magY, FREQ_BPFO, FREQ_BAND * 2);
  float bpfi_y = peakInBand(magY, FREQ_BPFI, FREQ_BAND * 2);
  s.bearing = max(max(bpfo_x, bpfi_x), max(bpfo_y, bpfi_y));

  // ── 2. UNBALANCE (Kode 2) ────────────────────────────────
  //  Ciri: 1× jauh lebih dominan dari 2× di sumbu radial
  float a1x = peakInBand(magX, FREQ_1X, FREQ_BAND);
  float a2x = peakInBand(magX, FREQ_2X, FREQ_BAND);
  float a1y = peakInBand(magY, FREQ_1X, FREQ_BAND);
  float a2y = peakInBand(magY, FREQ_2X, FREQ_BAND);
  bool dom1x = (a1x > AMP_UNBALANCE) && (a1x > a2x * 1.5f);
  bool dom1y = (a1y > AMP_UNBALANCE) && (a1y > a2y * 1.5f);
  // Skor = amplitudo 1× terbesar (jika memenuhi syarat dominansi)
  s.unbalance = 0.0f;
  if (dom1x) s.unbalance = max(s.unbalance, a1x);
  if (dom1y) s.unbalance = max(s.unbalance, a1y);

  // ── 3. MISALIGNMENT (Kode 3) ─────────────────────────────
  //  Ciri: 2× dan 3× dominan, terutama di sumbu aksial (Z)
  float a2z = peakInBand(magZ, FREQ_2X, FREQ_BAND);
  float a3z = peakInBand(magZ, FREQ_3X, FREQ_BAND);
  float a1z = peakInBand(magZ, FREQ_1X, FREQ_BAND);
  // Skor = rata-rata 2× dan 3× di Z (jika 2× > 1×)
  if (a2z > a1z * 1.2f) {
    s.misalign = (a2z + a3z) * 0.5f;
  } else {
    s.misalign = 0.0f;
  }

  // ── 4. RESONANSI STRUKTUR (Kode 4) ───────────────────────
  //  Ciri: puncak lebar di frekuensi natural (jauh di bawah 1×)
  float aRes = peakInBand(magX, FREQ_RESONANCE, 10.0f);
  s.resonance = aRes;

  // ── 5. KELONGGARAN MEKANIK (Kode 5) ──────────────────────
  //  Ciri: sub-harmonik 0.5× dan harmonik genap (2×, 4×)
  //  Sering disertai getaran impulsif acak (noise floor naik)
  float aHalf = peakInBand(magX, FREQ_HALF_X, FREQ_BAND);
  float a4x   = peakInBand(magX, FREQ_2X * 2.0f, FREQ_BAND); // 4×
  float a4y   = peakInBand(magY, FREQ_2X * 2.0f, FREQ_BAND);
  s.looseness = max(aHalf, max(a4x, a4y));

  return s;
}

// ═════════════════════════════════════════════════════════════
//  detectFaultCode() — FUNGSI UTAMA
// ─────────────────────────────────────────────────────────────
//  Mengembalikan satu nilai integer 0–5:
//    0 = Normal
//    1 = Kerusakan Bantalan
//    2 = Ketidakseimbangan Rotor
//    3 = Misalignment Poros
//    4 = Resonansi Struktur
//    5 = Kelonggaran Mekanik
//
//  Jika lebih dari satu kerusakan terdeteksi, fungsi memilih
//  kerusakan dengan SKOR TERTINGGI (kondisi paling parah).
// ═════════════════════════════════════════════════════════════
int detectFaultCode() {
  FaultScores s = calcScores();

  // Buat tabel: [threshold, skor, kode]
  struct Entry { float thr; float score; int code; };
  Entry table[5] = {
    { AMP_BEARING,   s.bearing,   FAULT_BEARING   },
    { AMP_UNBALANCE, s.unbalance, FAULT_UNBALANCE },
    { AMP_MISALIGN,  s.misalign,  FAULT_MISALIGN  },
    { AMP_RESONANCE, s.resonance, FAULT_RESONANCE },
    { AMP_LOOSENESS, s.looseness, FAULT_LOOSENESS },
  };

  // Cari kerusakan aktif dengan skor tertinggi
  int    bestCode  = FAULT_NORMAL;
  float  bestScore = 0.0f;

  for (int i = 0; i < 5; i++) {
    if (table[i].score >= table[i].thr && table[i].score > bestScore) {
      bestScore = table[i].score;
      bestCode  = table[i].code;
    }
  }

  return bestCode;
}

// ═════════════════════════════════════════════════════════════
//  Nama & Deskripsi Kode Kerusakan
// ═════════════════════════════════════════════════════════════
const char* faultName(int code) {
  switch (code) {
    case FAULT_BEARING:   return "Kerusakan Bantalan (Bearing Fault)";
    case FAULT_UNBALANCE: return "Ketidakseimbangan Rotor (Unbalance)";
    case FAULT_MISALIGN:  return "Misalignment Poros";
    case FAULT_RESONANCE: return "Resonansi Struktur";
    case FAULT_LOOSENESS: return "Kelonggaran Mekanik (Looseness)";
    default:              return "Normal — Tidak ada kerusakan";
  }
}

const char* faultAction(int code) {
  switch (code) {
    case FAULT_BEARING:   return "Segera periksa / ganti bantalan.";
    case FAULT_UNBALANCE: return "Lakukan balancing rotor.";
    case FAULT_MISALIGN:  return "Lakukan alignment kopling/poros.";
    case FAULT_RESONANCE: return "Periksa dudukan dan baut fondasi mesin.";
    case FAULT_LOOSENESS: return "Kencangkan semua baut dan periksa kopling.";
    default:              return "Lanjutkan operasi normal.";
  }
}

// ═════════════════════════════════════════════════════════════
//  Cetak Laporan Serial (teks)
// ═════════════════════════════════════════════════════════════
void printReport(const Axes& raw, int code) {
  FaultScores s = calcScores(); // untuk tampilkan skor detail

  Serial.println(F("╔══════════════════════════════════════════════╗"));
  Serial.println(F("║     DIAGNOSTIK MOTOR INDUKSI  v2.0           ║"));
  Serial.println(F("╠══════════════════════════════════════════════╣"));

  // Akselerasi mentah
  Serial.print(F("║  Accel X : ")); Serial.print(raw.x, 4); Serial.println(F(" g"));
  Serial.print(F("║  Accel Y : ")); Serial.print(raw.y, 4); Serial.println(F(" g"));
  Serial.print(F("║  Accel Z : ")); Serial.print(raw.z, 4); Serial.println(F(" g"));
  Serial.print(F("║  RMS X   : ")); Serial.print(s.rmsX, 4); Serial.println(F(" g"));
  Serial.print(F("║  RMS Y   : ")); Serial.print(s.rmsY, 4); Serial.println(F(" g"));
  Serial.print(F("║  RMS Z   : ")); Serial.print(s.rmsZ, 4); Serial.println(F(" g"));

  // Skor kerusakan
  Serial.println(F("╠──────────────────────────────────────────────╣"));
  Serial.println(F("║  SKOR KERUSAKAN (FFT amplitude)              ║"));
  Serial.print(F("║  [1] Bearing   : ")); Serial.println(s.bearing,   4);
  Serial.print(F("║  [2] Unbalance : ")); Serial.println(s.unbalance, 4);
  Serial.print(F("║  [3] Misalign  : ")); Serial.println(s.misalign,  4);
  Serial.print(F("║  [4] Resonansi : ")); Serial.println(s.resonance, 4);
  Serial.print(F("║  [5] Looseness : ")); Serial.println(s.looseness, 4);

  // Kode hasil akhir
  Serial.println(F("╠══════════════════════════════════════════════╣"));
  Serial.print(F("║  KODE KERUSAKAN : "));
  Serial.println(code);
  Serial.print(F("║  STATUS         : "));
  Serial.println(faultName(code));
  Serial.print(F("║  TINDAKAN       : "));
  Serial.println(faultAction(code));
  Serial.println(F("╚══════════════════════════════════════════════╝"));
  Serial.println();
}

// ═════════════════════════════════════════════════════════════
//  JSON Output — untuk dashboard / MQTT / Node-RED
//  Format: {"code":1,"name":"...","ax":...,"rmsX":...}
// ═════════════════════════════════════════════════════════════
void printJSON(const Axes& raw, int code) {
  FaultScores s = calcScores();
  Serial.print(F("{\"code\":"));
  Serial.print(code);
  Serial.print(F(",\"name\":\""));
  Serial.print(faultName(code));
  Serial.print(F("\",\"ax\":"));  Serial.print(raw.x, 4);
  Serial.print(F(",\"ay\":"));    Serial.print(raw.y, 4);
  Serial.print(F(",\"az\":"));    Serial.print(raw.z, 4);
  Serial.print(F(",\"rmsX\":"));  Serial.print(s.rmsX, 4);
  Serial.print(F(",\"rmsY\":"));  Serial.print(s.rmsY, 4);
  Serial.print(F(",\"rmsZ\":"));  Serial.print(s.rmsZ, 4);
  Serial.print(F(",\"scBearing\":")); Serial.print(s.bearing, 4);
  Serial.print(F(",\"scUnbal\":"));   Serial.print(s.unbalance, 4);
  Serial.print(F(",\"scMisal\":"));   Serial.print(s.misalign, 4);
  Serial.print(F(",\"scReson\":"));   Serial.print(s.resonance, 4);
  Serial.print(F(",\"scLoose\":"));   Serial.print(s.looseness, 4);
  Serial.println(F("}"));
}

// ═════════════════════════════════════════════════════════════
//  LED Blink sesuai kode (visual indicator tanpa delay blocking)
//  Kode 0 → OFF | Kode 1–5 → blink N kali, pause, ulangi
// ═════════════════════════════════════════════════════════════
void updateLED(int code) {
  static unsigned long ledTimer = 0;
  static int  blinkCount = 0;
  static bool ledState   = false;
  static bool inPause    = false;

  if (code == FAULT_NORMAL) {
    digitalWrite(LED_PIN, LOW);
    blinkCount = 0; inPause = false; ledState = false;
    return;
  }

  unsigned long now = millis();
  if (inPause) {
    if (now - ledTimer >= 1200) {        // jeda 1.2 detik antar siklus
      inPause = false; blinkCount = 0; ledTimer = now;
    }
    return;
  }

  if (blinkCount < code * 2) {           // setiap kode = N blink
    if (now - ledTimer >= 200) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      blinkCount++;
      ledTimer = now;
    }
  } else {
    digitalWrite(LED_PIN, LOW);
    inPause = true; ledTimer = now;
  }
}

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  if (!mpu.begin()) {
    Serial.println(F("[ERROR] MPU6050 tidak terdeteksi. Periksa wiring!"));
    while (1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(150);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);

  memset(vRealX, 0, sizeof(vRealX));
  memset(vRealY, 0, sizeof(vRealY));
  memset(vRealZ, 0, sizeof(vRealZ));

  Serial.println(F("=============================================="));
  Serial.println(F("  Motor Fault Detection ESP32 + MPU6050 v2.0"));
  Serial.println(F("  Kode: 0=Normal 1=Bearing 2=Unbalance"));
  Serial.println(F("        3=Misalign 4=Resonansi 5=Looseness"));
  Serial.println(F("=============================================="));
  Serial.println();

  lastSampleTime = micros();
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════
void loop() {
  // ── Sampling tepat waktu (non-blocking) ──────────────────
  unsigned long now = micros();
  if (now - lastSampleTime < SAMPLE_PERIOD_US) {
    updateLED(lastFaultCode);   // LED blink tetap berjalan
    return;
  }
  lastSampleTime += SAMPLE_PERIOD_US;

  // ── Baca sensor ───────────────────────────────────────────
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  Axes raw = {
    a.acceleration.x / 9.81f,
    a.acceleration.y / 9.81f,
    a.acceleration.z / 9.81f
  };

  // ── Isi buffer ────────────────────────────────────────────
  vRealX[bufIdx] = raw.x;
  vRealY[bufIdx] = raw.y;
  vRealZ[bufIdx] = raw.z;
  bufIdx++;

  if (bufIdx >= SAMPLES) { bufIdx = 0; bufFull = true; }

  // ── Analisis setiap 256 sampel (≈ setiap 256 ms) ─────────
  if (bufFull && bufIdx == 0) {
    bufFull = false;

    // ┌─────────────────────────────────────────────────────┐
    // │  PANGGIL detectFaultCode() — output: int 0 hingga 5 │
    // └─────────────────────────────────────────────────────┘
    int code = detectFaultCode();
    lastFaultCode = code;

    // Cetak kode singkat (satu baris — cocok untuk pembacaan
    // oleh mikrokontroler lain atau parser serial)
    Serial.print(F("FAULT_CODE:"));
    Serial.println(code);

    // Cetak laporan lengkap dan JSON
    printReport(raw, code);
    printJSON(raw, code);
  }

  updateLED(lastFaultCode);
}

/*
 * ============================================================
 *  CARA MENGGUNAKAN detectFaultCode() DI PROYEK LAIN
 * ─────────────────────────────────────────────────────────────
 *  int kode = detectFaultCode();
 *
 *  switch (kode) {
 *    case FAULT_NORMAL:    // 0 — motor sehat
 *    case FAULT_BEARING:   // 1 — ganti bantalan
 *    case FAULT_UNBALANCE: // 2 — balancing rotor
 *    case FAULT_MISALIGN:  // 3 — alignment poros
 *    case FAULT_RESONANCE: // 4 — periksa fondasi
 *    case FAULT_LOOSENESS: // 5 — kencangkan baut
 *  }
 *
 *  Fungsi wajib dipanggil setelah buffer 256 sampel terisi.
 *  Buffer diisi oleh loop() secara otomatis.
 *
 * ─────────────────────────────────────────────────────────────
 *  WIRING MPU6050 → ESP32
 *    VCC → 3.3V   GND → GND
 *    SDA → GPIO21   SCL → GPIO22
 *    AD0 → GND (alamat I2C: 0x68)
 *
 * ─────────────────────────────────────────────────────────────
 *  PARAMETER PENTING YANG PERLU DISESUAIKAN:
 *  • FREQ_1X     = RPM_motor / 60
 *    Contoh: 1500 RPM → 25 Hz | 3000 RPM → 50 Hz
 *  • FREQ_BPFO/I = hitung dari datasheet bantalan
 *    BPFO = (Nb/2)×(RPM/60)×(1 – Bd/Pd×cos α)
 *  • FREQ_RESONANCE = ukur dengan tap test atau FRF
 * ============================================================
 */

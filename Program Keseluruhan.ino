#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_MAX31865.h>
#include <LiquidCrystal_I2C.h>

#define TCA9548A_ADDR 0x70  // Alamat I2C Multiplexer

// Alamat I2C untuk DAC
#define DAC_ADDR_SETPOINT 0x60  // Alamat untuk setpoint
#define DAC_ADDR_SENSOR 0x61    // Alamat untuk sensor

// Inisialisasi LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Inisialisasi sensor suhu PT100
Adafruit_MAX31865 thermo = Adafruit_MAX31865(10, 11, 12, 13);
#define RREF 430.0
#define RNOMINAL 100.0

// Pin tombol dan relay
#define BTN_SUHU_PLUS 3    // Tombol suhu plus
#define BTN_SUHU_MINUS 4   // Tombol suhu minus
#define BTN_START 5
#define BTN_STOP 6
#define RELAY_PIN A3
#define SENSOR_KANAN 7
#define SENSOR_KIRI 8
#define RPM_SENSOR_PIN 2   // Pin untuk sensor RPM
#define MOTOR_KANAN A1
#define MOTOR_KIRI A2
#define BUZZER A0  
#define MOTOR_PWM 9       // Pin PWM untuk kontrol driver motor

unsigned long lastLCDUpdateTime = 0;
unsigned long lastSerialUpdateTime = 0;
const unsigned long lcdUpdateInterval = 1000; // update LCD setiap 1 detik
const unsigned long SerialUpdateInterval = 1000; // update LCD setiap 1 detik

bool sensor_kanan_terdeteksi = false;
bool sensor_kiri_terdeteksi = false;

// Variabel pembacaan RPM dengan metode interval waktu (dari program pertama)
unsigned long last_rpm_time = 0;
int last_rpm_state = LOW;
float current_rpm = 0.0;
bool firstPulse = true;

float suhu_setpoint = 20.0;
const int pwm_value = 110;  // Nilai PWM untuk motor (0-255) 140
bool system_running = false;

// Inisialisasi DAC - sekarang tanpa alamat
Adafruit_MCP4725 dac;

unsigned long last_time_suhu = 0;
unsigned long last_time_rpm = 0;
bool safety_suhu = false;
bool safety_rpm = false;

void selectChannel(uint8_t channel) {
    Wire.beginTransmission(TCA9548A_ADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

// **Fungsi Inisialisasi Sistem tanpa mengaktifkan sistem**
void initSystem() {
    if (digitalRead(SENSOR_KANAN) == HIGH) {
        sensor_kanan_terdeteksi = true;
        sensor_kiri_terdeteksi = false;
    } else if (digitalRead(SENSOR_KIRI) == HIGH) {
        sensor_kiri_terdeteksi = true;
        sensor_kanan_terdeteksi = false;
    } else {
        // Jika tidak ada sensor yang aktif, paksa motor ke kanan dulu
        sensor_kiri_terdeteksi = true;
        sensor_kanan_terdeteksi = false;
    }

    system_running = false;  // Pastikan sistem TIDAK berjalan pada inisialisasi
    safety_suhu = false;
    safety_rpm = false;
    
    // Reset variabel RPM
    last_rpm_time = 0;
    last_rpm_state = LOW;
    current_rpm = 0.0;
    firstPulse = true;
}

// **Fungsi Reset Sistem** - dipanggil saat tombol START ditekan
void resetSistem() {
    if (digitalRead(SENSOR_KANAN) == HIGH) {
        sensor_kanan_terdeteksi = true;
        sensor_kiri_terdeteksi = false;
    } else if (digitalRead(SENSOR_KIRI) == HIGH) {
        sensor_kiri_terdeteksi = true;
        sensor_kanan_terdeteksi = false;
    } else {
        // Jika tidak ada sensor yang aktif, paksa motor ke kanan dulu
        sensor_kiri_terdeteksi = true;
        sensor_kanan_terdeteksi = false;
    }

    system_running = true;  // Mulai sistem
    last_time_suhu = millis();
    last_time_rpm = millis();
    safety_suhu = false;
    safety_rpm = false;
    
    // Reset variabel RPM
    last_rpm_time = 0;
    last_rpm_state = LOW;
    current_rpm = 0.0;
    firstPulse = true;
}

void setup() {
    Serial.begin(115200);
    Wire.begin();
    delay(100);  // Beri waktu untuk I2C
    
    lcd.begin();
    lcd.backlight();
    delay(100);  // Beri waktu untuk LCD
    
    // Inisialisasi MAX31865
    thermo.begin(MAX31865_3WIRE);
    delay(100);  // Beri waktu untuk sensor suhu

    // Setup tombol dan relay
    pinMode(BTN_SUHU_PLUS, INPUT_PULLUP);   // Tombol suhu plus
    pinMode(BTN_SUHU_MINUS, INPUT_PULLUP);  // Tombol suhu minus
    pinMode(BTN_START, INPUT_PULLUP);
    pinMode(BTN_STOP, INPUT_PULLUP);
    pinMode(SENSOR_KANAN, INPUT);
    pinMode(SENSOR_KIRI, INPUT);
    pinMode(RPM_SENSOR_PIN, INPUT);         // Set pin sensor RPM sebagai input
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(MOTOR_KANAN, OUTPUT);
    pinMode(MOTOR_KIRI, OUTPUT);
    pinMode(BUZZER, OUTPUT);
    pinMode(MOTOR_PWM, OUTPUT);            // Set pin PWM sebagai output

    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(MOTOR_KANAN, LOW);
    digitalWrite(MOTOR_KIRI, LOW);
    digitalWrite(BUZZER, LOW);
    analogWrite(MOTOR_PWM, 0);            // Set PWM ke 0 saat awal

    // Inisialisasi DAC untuk setiap channel yang digunakan
    // Inisialisasi untuk channel 0 (sensor suhu)
    selectChannel(0);
    dac.begin(DAC_ADDR_SENSOR);  // Gunakan alamat 0x61 untuk sensor
    dac.setVoltage(0, false);
    delay(50);
    
    // Inisialisasi untuk channel 2 (setpoint suhu)
    selectChannel(2);
    dac.begin(DAC_ADDR_SETPOINT);  // Gunakan alamat 0x60 untuk setpoint
    dac.setVoltage(0, false);
    delay(50);

    // Panggil initSystem sebagai pengganti resetSistem
    initSystem();

    lcd.setCursor(4, 0);
    lcd.print("Inkubator");
    lcd.setCursor(4, 1);
    lcd.print("Agitator");
    delay(3000);

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Avesina Tsalasa");
    lcd.setCursor(1, 1);
    lcd.print("Clarita Adelia");
    delay(2000);

    // Tampilkan pesan awal pada LCD
    lcd.clear();
}

void loop() {
    unsigned long currentTime = millis();
    
    // Baca sensor RPM dengan metode interval waktu
    updateRPM();
    
    // Baca suhu dari PT100
    float suhu_aktual = thermo.temperature(RNOMINAL, RREF);

    if (currentTime - lastSerialUpdateTime >= SerialUpdateInterval) {
        Serial.println(suhu_aktual);
        lastSerialUpdateTime = currentTime;
    }

    // Hitung tegangan sensor suhu
    float volt_sensor_suhu = mapfloat(suhu_aktual, 0, 50, 0, 5) * 1.24;

    // Hitung tegangan setpoint suhu
    float volt_setpoint_suhu = mapfloat(suhu_setpoint, 0, 50, 0, 5) * 1.26;
    
    // Update DAC dengan alamat berbeda untuk setpoint dan sensor
    setDAC(2, volt_setpoint_suhu * 819, DAC_ADDR_SETPOINT);  // Setpoint suhu - alamat 0x60
    setDAC(0, volt_sensor_suhu * 819, DAC_ADDR_SENSOR);      // Suhu aktual - alamat 0x61

    // Selalu update tegangan sensor jika sistem menyala
    if (system_running) {
        // Cek suhu - apakah di luar rentang ±1°C dari setpoint
        if (suhu_aktual > (suhu_setpoint + 0.5) || suhu_aktual < (suhu_setpoint - 0.5)) {
            // Jika suhu di luar rentang yang diizinkan, aktifkan safety setelah delay
            if (!safety_suhu) {
                // Jika safety suhu belum aktif, mulai penghitungan waktu
                if (last_time_suhu == 0) {
                    last_time_suhu = currentTime;
                }
                // Cek apakah sudah lewat 2 detik
                else if (currentTime - last_time_suhu >= 60000) {
                    safety_suhu = true; // Aktifkan safety suhu
                }
            }
        } else {
            // Suhu kembali normal, reset timer dan flag
            last_time_suhu = 0;
            safety_suhu = false;
        }
        
        // Cek RPM - apakah di luar rentang 55-65 RPM
        if (current_rpm > 65 || current_rpm < 55) {
            // Jika RPM di luar rentang yang diizinkan, aktifkan safety setelah delay
            if (!safety_rpm) {
                // Jika safety rpm belum aktif, mulai penghitungan waktu
                if (last_time_rpm == 0) {
                    last_time_rpm = currentTime;
                }
                // Cek apakah sudah lewat 2 detik
                else if (currentTime - last_time_rpm >= 300000) {
                    safety_rpm = true; // Aktifkan safety rpm
                }
            }
        } else {
            // RPM kembali normal, reset timer dan flag
            last_time_rpm = 0;
            safety_rpm = false;
        }
    }

    // Buzzer dan safety checks
    if (system_running && (safety_suhu || safety_rpm)) {
        digitalWrite(RELAY_PIN, LOW);
        digitalWrite(MOTOR_KANAN, LOW);
        digitalWrite(MOTOR_KIRI, LOW);
        analogWrite(MOTOR_PWM, 0);  // Matikan PWM
        
        digitalWrite(BUZZER, HIGH);
        delay(600);
        digitalWrite(BUZZER, LOW);
        delay(200);

        // Update LCD hanya setiap 1 detik
        if (currentTime - lastLCDUpdateTime >= lcdUpdateInterval) {
            lastLCDUpdateTime = currentTime;
            lcd.clear();
            if (safety_suhu) {
                lcd.setCursor(3, 0);
                lcd.print("Suhu tidak");
                lcd.setCursor(4, 1);
                lcd.print("tercapai");
            }
            if (safety_rpm) {
                lcd.setCursor(3, 0);
                lcd.print("RPM tidak");
                lcd.setCursor(4, 1);
                lcd.print("tercapai");
            }
        }
    } else {
        digitalWrite(BUZZER, LOW);
    }

    // Cek tombol suhu plus
    if (digitalRead(BTN_SUHU_PLUS) == LOW) {
        suhu_setpoint += 1.0;
        if (suhu_setpoint >= 24.05) suhu_setpoint = 24.0;  // Batasi maksimum 24
        delay(50); // Mini delay hanya untuk debouncing
    }

    // Cek tombol suhu minus
    if (digitalRead(BTN_SUHU_MINUS) == LOW) {
        suhu_setpoint -= 1.0;
        if (suhu_setpoint <= 19.95) suhu_setpoint = 20.0;  // Batasi minimum 20
        delay(50); // Mini delay hanya untuk debouncing
    }

    // Tombol START ditekan
    if (digitalRead(BTN_START) == LOW) {
        if (!system_running) {
            resetSistem();
            digitalWrite(RELAY_PIN, HIGH);
            analogWrite(MOTOR_PWM, pwm_value);  // Keluarkan PWM saat START ditekan
            
            // Tambahkan konfirmasi di LCD
            lcd.clear();
            lcd.setCursor(2, 0);
            lcd.print("MULAI SISTEM");
            delay(1000);
        }

        delay(50); // Mini delay hanya untuk debouncing
    }

    // Tombol STOP ditekan
    if (digitalRead(BTN_STOP) == LOW) {
        system_running = false;
        digitalWrite(RELAY_PIN, LOW);
        digitalWrite(MOTOR_KANAN, LOW);
        digitalWrite(MOTOR_KIRI, LOW);
        digitalWrite(BUZZER, LOW);
        analogWrite(MOTOR_PWM, 0);  // Matikan PWM

        safety_suhu = false;
        safety_rpm = false;
        
        // Tambahkan konfirmasi di LCD
        lcd.clear();
        lcd.setCursor(2, 0);
        lcd.print("RESET SISTEM");
        delay(1000);
        
        delay(50); // Mini delay hanya untuk debouncing
    }

    // Baca sensor laser - TIDAK ADA DELAY, langsung baca dan proses
    if (system_running) {
        // Pembacaan sensor tanpa delay
        if (digitalRead(SENSOR_KANAN) == HIGH) {
            sensor_kanan_terdeteksi = true;
            sensor_kiri_terdeteksi = false;
        } else if (digitalRead(SENSOR_KIRI) == HIGH) {
            sensor_kiri_terdeteksi = true;
            sensor_kanan_terdeteksi = false;
        }

        // Gerakkan motor sesuai sensor - respon cepat
        if (sensor_kanan_terdeteksi) {
            digitalWrite(MOTOR_KANAN, HIGH);
            digitalWrite(MOTOR_KIRI, LOW);
        } else if (sensor_kiri_terdeteksi) {
            digitalWrite(MOTOR_KANAN, LOW);
            digitalWrite(MOTOR_KIRI, HIGH);
        } else {
            digitalWrite(MOTOR_KANAN, HIGH);
            digitalWrite(MOTOR_KIRI, LOW);
        }
    }

    // Update tampilan LCD setiap 1 detik
    if (currentTime - lastLCDUpdateTime >= lcdUpdateInterval) {
        lastLCDUpdateTime = currentTime;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("T:"); lcd.print(suhu_aktual); lcd.print("C ");
            lcd.setCursor(9, 0);
            lcd.print("S:"); lcd.print(suhu_setpoint); lcd.print("C");
            lcd.setCursor(0, 1);
            lcd.print("R:"); lcd.print(current_rpm); lcd.print("RPM");
    }
    // Tidak ada delay di akhir loop - program berjalan tanpa hambatan
}

// Fungsi untuk memperbarui nilai RPM dengan metode interval waktu (dari program pertama)
// Dimodifikasi untuk menggunakan logika HIGH untuk deteksi
void updateRPM() {
  // Hanya proses jika sistem berjalan
  if (!system_running) {
    current_rpm = 0;
    return;
  }
  
  // Baca state sensor RPM saat ini
  int rpmState = digitalRead(RPM_SENSOR_PIN);
  
  // Deteksi perubahan state dari LOW ke HIGH (rising edge) untuk logika HIGH
  if (rpmState == HIGH && last_rpm_state == LOW) {
    // Debouncing - pastikan tidak ada noise
    delayMicroseconds(500);
    
    // Cek kembali untuk memastikan bukan noise
    if (digitalRead(RPM_SENSOR_PIN) == HIGH) {
      unsigned long currentTime = millis();
      
      // Jika ini bukan pulsa pertama, hitung interval
      if (!firstPulse && last_rpm_time > 0) {
        // Hitung interval antara pulsa (dalam milidetik)
        unsigned long pulseInterval = currentTime - last_rpm_time;
        
        // Hitung RPM berdasarkan interval waktu: RPM = (60 * 1000) / interval_ms
        // 60 untuk konversi dari putaran per detik ke putaran per menit
        // 1000 untuk konversi dari milidetik ke detik
        if (pulseInterval > 0) {
          float newRpm = (60.0 * 1000.0) / pulseInterval;
          
          // Pastikan nilai RPM masuk akal (misalnya, tidak terlalu tinggi karena noise)
          if (newRpm <= 500) { // Batas atas yang masuk akal
            // Filter nilai RPM untuk menghindari fluktuasi besar
            // Gunakan low-pass filter sederhana
            static float filtered_rpm = 0;
            filtered_rpm = 0.7 * filtered_rpm + 0.3 * newRpm; // 70% nilai lama, 30% nilai baru
            current_rpm = filtered_rpm * 1.38;
          }
        }
      } else {
        firstPulse = false;
      }
      
      // Simpan waktu pulsa ini untuk perhitungan berikutnya
      last_rpm_time = currentTime;
    }
  }
  
  // Simpan state terakhir untuk deteksi edge berikutnya
  last_rpm_state = rpmState;
  
  // Jika tidak ada pulsa dalam waktu yang lama, anggap RPM = 0
  unsigned long currentTime = millis();
  if (currentTime - last_rpm_time > 3000 && last_rpm_time > 0) { // 3 detik tanpa pulsa
    current_rpm = 0.0;
  }
}

// Fungsi mapping float
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Fungsi mengatur MCP4725 melalui multiplexer dengan alamat yang ditentukan
void setDAC(uint8_t channel, uint16_t value, uint8_t address) {
    selectChannel(channel);
    dac.begin(address);  // Gunakan alamat yang diberikan
    dac.setVoltage(value, false);
}

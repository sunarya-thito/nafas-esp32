// Pin yang bisa dipakai I/O tanpa inteferensi
// 4, 13, 16 - 33

#include <DHT.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ErriezMHZ19B.h>
#include <SD_ZH03B.h>
#include <esp_rom_uart.h>
#include <SoftwareSerial.h>
#include <FastLED.h>

#define HASH_SALT "97u31q2y8eh31hu2r3h9eudib8"
#define HASH_USERNAME_SHIFT 5
#define HASH_PASSWORD_SHIFT 3

#define ONLINE true

#define DHT_PIN 23
#define GAS_PIN 32
#define BUZZER_PIN 26
#define DUST_RX_PIN 16 // pin RX Serial 2
#define DUST_TX_PIN 17 // pin TX Serial 2
#define CO2_RX_PIN 18
#define CO2_TX_PIN 19

#define BUZZER_MAX_FREQ 3000
#define BUZZER_MIN_FREQ 200
#define BUZZER_DURATION 600

#define ID_TEMPERATURE "temp"
#define ID_HUMIDITY "humid"
#define ID_GAS "gas"
#define ID_CO2 "co2"
#define ID_DUST "dust"

#define KEY_MQTT_CONNECT "mqttConnect"
#define KEY_MQTT_HOSTNAME "mqttHostname"
#define KEY_MQTT_PORT "mqttPort"
#define KEY_WIFI_CONNECT "wifiConnect"
#define KEY_WIFI_SSID "wifiSSID"
#define KEY_WIFI_PASSWORD "wifiPassword"

#define COMMAND_DATA_UPLOAD "data"

#define STATUS_DISCONNECTED 0
#define STATUS_CONNECTING 1
#define STATUS_CONNECTED 2

#define LED_FADE_SPEED 5000

DHT dht(DHT_PIN, DHT22);
Preferences preferences;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Dust Sensor
SD_ZH03B dustSensor(Serial2);

// CO2 Sensor
EspSoftwareSerial::UART co2Serial;
ErriezMHZ19B co2Sensor(&co2Serial);

float previousHumid;
float previousTemp;
unsigned int previousGas;
unsigned int previousCO2;
unsigned int previousDust;
bool danger = false;
bool isBanger = false;

int currentNote = 0;
int nextNoteTime = 0;

int wifiConnected = 0;
int mqttConnected = 0;

unsigned long previousTickTimeSeconds = 0;

bool verbose = false;

CRGB leds[73];

int counter = 0;
double hue = 0;
double sat = 0;
double val = 0;

int targetHue = -1;
int targetSat = -1;
int targetVal = -1;

int previousTime = 0;

void debug(String s) {
  if (verbose) {
    Serial.println(s);
  }
}

void setup() {
  FastLED.addLeds<WS2812B, 22, GRB>(leds, 73);
  FastLED.show();
  pinMode(22, OUTPUT);
  // uart_set_pin(1, CO2_TX_PIN, CO2_RX_PIN, -1, -1);
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, DUST_RX_PIN, DUST_TX_PIN);
  // Serial1.begin(9600, SERIAL_8N1, CO2_RX_PIN, CO2_TX_PIN);
  co2Serial.begin(9600, EspSoftwareSerial::SWSERIAL_8N1, CO2_RX_PIN, CO2_TX_PIN);
  delay(1000);

  WiFi.setHostname(("Nafas " + String(ESP.getEfuseMac())).c_str());
  dustSensor.setMode(SD_ZH03B::IU_MODE);

  co2Sensor.setAutoCalibration(true);  
  
  Serial.println("Device ID: " + String(ESP.getEfuseMac()));
  pinMode(GAS_PIN, INPUT);
  pinMode(DHT_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  dht.begin();
  preferences.begin("nafas", false);
  isBanger = true;

  // while (!co2Sensor.detect()) {
  //   Serial.println("Detecting MH-Z19B sensor...");
  //   delay(2000);
  // }

  targetHue = preferences.getInt("hue", -1);
  targetSat = preferences.getInt("sat", -1);
  targetVal = preferences.getInt("val", -1);

  Serial.println("Ready!");
}

String username() {
  unsigned long hash = 0;
  String username = HASH_SALT + String(ESP.getEfuseMac());
  for (size_t i = 0; i < username.length(); i++) {
    char currentChar = username.charAt(i);
    hash = (hash << HASH_USERNAME_SHIFT) - hash + currentChar;
  }
  String hexString = String(hash, HEX);
  return hexString;
}

String password() {
  unsigned long hash = 0;
  String password = HASH_SALT + String(ESP.getEfuseMac());
  for (size_t i = 0; i < password.length(); i++) {
    char currentChar = password.charAt(i);
    hash = (hash << HASH_PASSWORD_SHIFT) - hash + currentChar;
  }
  String hexString = String(hash, HEX);
  return hexString;
}

int countArguments(String argumentsString) {
  int count = 0;
  for (int i = 0; i < argumentsString.length(); i++) {
    if (argumentsString.charAt(i) == ' ') {
      count++;
    }
  }
  return count + 1;
}

int clamp(int min, int val, int max) {
  return val < min ? min : max < val ? max : val;
}

void loop() {
  int currentTime = millis();
  double delta = currentTime - previousTime;
  previousTime = currentTime;

  for (int i = 0; i < 73; i++) {
    if (targetHue == -1 && targetSat == -1 && targetVal == -1) {
      hue = (counter + i * 4) % 360;
      sat = 255;
      val = 255;
      // Serial.println(hue);
    } else {
      double deltaHue = targetHue - hue;
      double deltaSat = targetSat - sat;
      double deltaVal = targetVal - val;

      double incHue = deltaHue / delta / LED_FADE_SPEED;
      double incSat = deltaSat / delta / LED_FADE_SPEED;
      double incVal = deltaVal / delta / LED_FADE_SPEED;

      hue += incHue;
      sat += incSat;
      val += incVal;

      // Serial.println(String(targetHue) + " " + String(targetSat) + " " + String(targetVal) + " " + String(hue) + " " + String(sat) + " " + String(val));
    }

    // hue = clamp(0, hue, 255);
    if (hue > 255) {
      hue = 0;
    } else if (hue < 0) {
      hue = 255;
    }
    // sat = clamp(0, sat, 255);
    // val = clamp(0, val, 255);

    leds[i] = CHSV((int) hue, (int) sat, (int) val);
  }
  counter++;
  FastLED.show();
  loopBanger();
  client.loop();
  unsigned long currentMillis = millis();
  unsigned long currentSeconds = currentMillis / 1000;
  while (previousTickTimeSeconds < currentSeconds) {
    loopSec(previousTickTimeSeconds++);
  }
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    executeRawCommand(command);
  }
  if (danger) {
    double tick = currentMillis % BUZZER_DURATION;
    double freq;
    if (tick < BUZZER_DURATION / 2) {
      freq = BUZZER_MIN_FREQ + (tick / (BUZZER_DURATION / 2)) * (BUZZER_MAX_FREQ - BUZZER_MIN_FREQ);
    } else {
      freq = BUZZER_MIN_FREQ + ((BUZZER_DURATION - tick) / (BUZZER_DURATION / 2)) * (BUZZER_MAX_FREQ - BUZZER_MIN_FREQ);
    }
    tone(BUZZER_PIN, round(freq), 10);
  }
}

void executeRawCommand(String command) {
  int spaceIndex = command.indexOf(' ');
  if (spaceIndex != -1) {
    String commandName = command.substring(0, spaceIndex);
    String argumentString = command.substring(spaceIndex + 1);
    int numArgs = countArguments(argumentString);
    String args[numArgs];
    for (int i = 0; i < numArgs; i++) {
      int nextSpace = argumentString.indexOf(' ');
      if (nextSpace != -1) {
        args[i] = argumentString.substring(0, nextSpace);
        argumentString = argumentString.substring(nextSpace + 1);
      } else {
        args[i] = argumentString;
      }
    }
    executeCommand(commandName, numArgs, args);
  } else {
    executeCommand(command, 0, nullptr);
  }
}

String readRest(int pos, int numArgs, String args[]) {
  String builder = "";
  for (int i = pos; i < numArgs; i++) {
    if (i > pos) {
      builder += " ";
    }
    builder += args[i];
  }
  return builder;
}

void executeCommand(String commandName, int numArgs, String args[]) {
  if (commandName.equalsIgnoreCase("wifi")) {
    if (numArgs > 0) {
      if (args[0].equalsIgnoreCase("connect")) {
        Serial.println("Menghubungkan jaringan WiFi");
        preferences.putBool(KEY_WIFI_CONNECT, true);
        return;
      }
      if (args[0].equalsIgnoreCase("set")) {
        if (numArgs > 1) {
          if (args[1].equalsIgnoreCase("ssid")) {
            preferences.putString(KEY_WIFI_SSID, readRest(2, numArgs, args));
            Serial.println("SSID berhasil diubah! Gunakan \"wifi connect\" untuk menghubungkan kembali dengan SSID yang baru.");
            return;
          }
          if (args[1].equalsIgnoreCase("password")) {
            preferences.putString(KEY_WIFI_PASSWORD, readRest(2, numArgs, args));
            Serial.println("Password berhasil diubah!");
            return;
          }
        }
      }
      if (args[0].equalsIgnoreCase("get")) {
        if (numArgs > 1) {
          if (args[1].equalsIgnoreCase("ssid")) {
            Serial.println("SSID: " + preferences.getString(KEY_WIFI_SSID));
            return;
          }
          if (args[1].equalsIgnoreCase("password")) {
            String pass = preferences.getString(KEY_WIFI_PASSWORD);
            if (pass == NULL) {
              Serial.println("Tidak ada password tersimpan.");
              return;
            }
            String hidden = "";
            for (int i = 0; i < pass.length(); i++) {
              hidden += "*";
            }
            Serial.println("Password: " + hidden);
            return;
          }
        }
      }
      if (args[0].equalsIgnoreCase("scan")) {
        int scanned = WiFi.scanNetworks();
        if (scanned == 0) {
          Serial.println("- Tidak ada jaringan WiFi yang tersedia -");
        } else {
          Serial.println("Terdapat " + String(scanned) + " jaringan WiFi yang tersedia:");
          for (int i = 0; i < scanned; i++) {
            Serial.println(String(i + 1) + ". " + WiFi.SSID(i));
          }
        }
        return;
      }
      if (args[0].equalsIgnoreCase("disconnect")) {
        Serial.println("Memutuskan jaringan wifi...");
        preferences.putBool(KEY_WIFI_CONNECT, false);
        wifiConnected = STATUS_DISCONNECTED;
        return;
      }
      if (args[0].equalsIgnoreCase("status")) {
        if (WiFi.status() != WL_CONNECTED) {
          Serial.println("Tidak terhubung ke jaringan manapun.");
        } else {
          Serial.println("Terhubung ke jaringan: " + WiFi.SSID());
        }
        return;
      }
    }
    Serial.println("Tulis \"help wifi\" untuk melihat cara pemakaian perintah wifi.");
    return;
  }
  if (commandName.equalsIgnoreCase("mqtt")) {
    if (numArgs > 0) {
      if (args[0].equalsIgnoreCase("connect")) {
        Serial.println("Menghubungkan ke MQTT broker...");
        preferences.putBool(KEY_MQTT_CONNECT, true);
        return;
      }
      if (args[0].equalsIgnoreCase("disconnect")) {
        Serial.println("Memutuskan MQTT...");
        preferences.putBool(KEY_MQTT_CONNECT, false);
        return;
      }
      if (args[0].equalsIgnoreCase("set")) {
        if (numArgs > 1) {
          if (args[1].equalsIgnoreCase("hostname")) {
            String hostname = readRest(2, numArgs, args);
            preferences.putString(KEY_MQTT_HOSTNAME, hostname);
            Serial.println("Berhasil mengubah MQTT Hostname ke: " + hostname);
            return;
          }
          if (args[1].equalsIgnoreCase("port")) {
            String port = readRest(2, numArgs, args);
            int parsedPort = port.toInt();
            preferences.putInt(KEY_MQTT_PORT, parsedPort);
            Serial.println("Berhasil mengubah MQTT Port ke: " + port);
            return;
          }
        }
      }
      if (args[0].equalsIgnoreCase("get")) {
        if (numArgs > 1) {
          if (args[1].equalsIgnoreCase("hostname")) {
            Serial.println("MQTT Hostname: " + preferences.getString(KEY_MQTT_HOSTNAME));
            return;
          }
          if (args[1].equalsIgnoreCase("port")) {
            Serial.println("MQTT Port: " + preferences.getInt(KEY_MQTT_PORT));
            return;
          }
        }
      }
      if (args[0].equalsIgnoreCase("status")) {
        if (client.connected()) {
          Serial.println("Status: Terhubung dengan broker");
        } else {
          Serial.println("Status: Tidak terhubung dengan broker");
        }
        return;
      }
    }
    Serial.println("Tulis \"help mqtt\" untuk melihat cara pemakaian perintah mqtt.");
    return;
  }
  if (commandName.equalsIgnoreCase("device")) {
    Serial.println("Device ID: " + String(ESP.getEfuseMac()));
    return;
  }
  if (commandName.equalsIgnoreCase("buzz")) {
    int duration = 1000;
    if (numArgs > 0) {
      duration = args[0].toInt();
    }
    buzz(duration);
    Serial.println("Buzzed!");
    return;
  }
  if (commandName.equalsIgnoreCase("danger")) {
    if (numArgs > 0) {
      danger = args[0].equalsIgnoreCase("on");
      return;
    }
    Serial.println("Tulis \"help danger\" untuk melihat cara pemakaian perintah danger.");
    return;
  }
  if (commandName.equalsIgnoreCase("led")) {
    if (numArgs > 2) {
      int parsedHue = args[0].toInt();
      int parsedSat = args[1].toInt();
      int parsedVal = args[2].toInt();
      targetHue = parsedHue;
      targetSat = parsedSat;
      targetVal = parsedVal;
      preferences.putInt("hue", hue);
      preferences.putInt("sat", sat);
      preferences.putInt("val", val);
      Serial.println("Berhasil mengubah LED ke H: " + String(parsedHue) + ", S: " + String(parsedSat) + ", V: " + String(parsedVal));
      return;
    }
    Serial.println("Tulis \"help led\" untuk melihat cara pemakaian perintah led.");
    return;
  }
  if (commandName.equalsIgnoreCase("verbose")) {
    verbose = !verbose;
    if (verbose) {
      Serial.println("Enabled verbose");
    } else {
      Serial.println("Disabled verbose");
    }
    return;
  }
  if (commandName.equalsIgnoreCase("banger")) {
    isBanger = !isBanger;
    if (!isBanger) {
      currentNote = 0;
      nextNoteTime = 0;
    }
    return;
  }
  if (commandName.equalsIgnoreCase("help")) {
    Serial.println("Daftar perintah:");
    if (numArgs == 0) {
      Serial.println("help [konteks] - Menampilkan daftar perintah");
      Serial.println("wifi - Pengelolaan koneksi WiFi");
      Serial.println("mqtt - Pengelolaan MQTT");
      Serial.println("device - Tampilkan device ID");
      Serial.println("buzz - Bunyikan buzzer");
      Serial.println("danger - Tetapkan status bahaya");
      Serial.println("led - Tetapkan warna LED");
      Serial.println("verbose - Toggle verbose");
    } else {
      if (args[0].equalsIgnoreCase("wifi")) {
        Serial.println("wifi connect - Hubungkan dengan WiFi");
        Serial.println("wifi set ssid <ssid> - Ubah SSID yang akan dihubungkan");
        Serial.println("wifi set password <password> - Ubah password untuk terhubung ke WiFi");
        Serial.println("wifi get ssid - Lihat SSID yang digunakan");
        Serial.println("wifi get passwrod - Lihat password yang digunakan");
        Serial.println("wifi scan - Melihat daftar WiFi yang tersedia");
        Serial.println("wifi disconnect - Putuskan jaringan WiFi");
        Serial.println("wifi status - Lihat status WiFi");
      } else if (args[0].equalsIgnoreCase("mqtt")) {
        Serial.println("mqtt connect - Nyalakan MQTT Client");
        Serial.println("mqtt disconnect - Matikan MQTT");
        Serial.println("mqtt set hostname - Ubah MQTT Hostname");
        Serial.println("mqtt set port - Ubah MQTT Port");
        Serial.println("mqtt get hostname - Tampilkan MQTT Hostname");
        Serial.println("mqtt get port - Tampilkan MQTT Port");
        Serial.println("mqtt status - Tampilkan status MQTT");
      } else if (args[0].equalsIgnoreCase("device")) {
        Serial.println("device - Tampilkan device ID");
      } else if (args[0].equalsIgnoreCase("buzz")) {
        Serial.println("buzz [durasi] - Bunyikan buzzer");
      } else if (args[0].equalsIgnoreCase("danger")) {
        Serial.println("danger on - Tetapkan status bahaya");
        Serial.println("danger off - Tetapkan status aman");
      } else if (args[0].equalsIgnoreCase("led")) {
        Serial.println("led <hue> <sat> <val> - Tetapkan warna LED (Gunakan -1 untuk mereset)");
      }
    }
    Serial.println("<> - Argumen wajib diisi");
    Serial.println("[] - Opsional");
    return;
  }
  Serial.println("Tulis \"help\" untuk melihat semua perintah.");
  return;
}

void loopSec(int tickTimeSeconds) {
  bool wifiConnect = preferences.getBool(KEY_WIFI_CONNECT);
  if (wifiConnect) {
    int status = WiFi.status();
    if (wifiConnected == STATUS_CONNECTED && status != WL_CONNECTED) {
      wifiConnected = STATUS_DISCONNECTED;
    }
    if (wifiConnected == STATUS_DISCONNECTED) {
      wifiConnected = STATUS_CONNECTING;
      String ssid = preferences.getString(KEY_WIFI_SSID);
      String password = preferences.getString(KEY_WIFI_PASSWORD);
      if (ssid != NULL) {
        Serial.println("Menghubungkan ke jaringan WiFi \"" + ssid + "\"...");
        WiFi.begin(ssid, password);
      }
    }
    if (status == WL_CONNECTED && wifiConnected != STATUS_CONNECTED) {
      wifiConnected = STATUS_CONNECTED;
      Serial.println("Terhubung ke jaringan WiFi!");
    }
  } else {
    if (wifiConnected == STATUS_CONNECTED) {
      wifiConnected = STATUS_DISCONNECTED;
      WiFi.disconnect();
      Serial.println("Terputus dari jaringan WiFi!");
    }
  }

  bool mqttConnect = preferences.getBool(KEY_MQTT_CONNECT);
  try {
    if (mqttConnect) {
      int state = client.state();
      if (mqttConnected == STATUS_CONNECTED && state != MQTT_CONNECTED) {
        mqttConnected = STATUS_DISCONNECTED;
      }
      if (mqttConnected == STATUS_DISCONNECTED) {
        mqttConnected = STATUS_CONNECTING;
        String hostname = preferences.getString("mqttHostname");
        int port = preferences.getInt("mqttPort", 1883);
        if (hostname == NULL) {
          Serial.println("MQTT Hostname is empty");
          mqttConnected = STATUS_DISCONNECTED;
          return;
        }
        Serial.println("Menghubungkan ke MQTT Broker (" + hostname +":" + String(port) + ")");
        client.setCallback(mqttCallback);
        client.setServer(hostname.c_str(), port);
        String clientId = String(ESP.getEfuseMac());
        String uname = username();
        String pwd = password();
        if (client.connect(clientId.c_str(), uname.c_str(), pwd.c_str())) {
          Serial.println("Terhubung dengan MQTT broker");
          if (!client.subscribe(("nafas_" + String(ESP.getEfuseMac())).c_str())) {
            Serial.println("Failed to subscribe to topic");
          }
          mqttConnected = STATUS_CONNECTED;
        } else {
          Serial.print("Gagal menghubungkan dengan MQTT broker: ");
          Serial.println(client.state());
          mqttConnected = STATUS_DISCONNECTED;
        }
      }
    } else {
      if (mqttConnected == STATUS_CONNECTED) {
        mqttConnected = STATUS_DISCONNECTED;
        client.disconnect();
        Serial.println("Terputus dengan MQTT broker");
      }
    }
  } catch (String e) {
    Serial.println("Error " + e);
  }

  

  float humid = dht.readHumidity();
  float temp = dht.readTemperature();
  if (isnanf(humid)) {
    humid = 0;
  }
  if (isnanf(temp)) {
    temp = 0;
  }

  unsigned int readGas = analogRead(GAS_PIN);
  unsigned int mappedGas = map(readGas, 0, 1023, 0, 255);

  int dust = dustSensor.readData() ? dustSensor.getPM2_5() : -1;
  if (dust < 0) {
    dust = previousDust;
  }

  int co2 = -1;

  if (co2Sensor.isReady()) {
    co2 = co2Sensor.readCO2();
    if (co2 < 0) {
      switch (co2) {
        case MHZ19B_RESULT_ERR_CRC:
          Serial.println("MHZ19B: CRC Error");
          break;
        case MHZ19B_RESULT_ERR_TIMEOUT:
          Serial.println("RX timeout");
          break;
        default:
          Serial.println("Error: " + String(co2));
          break;
      }
    }
  }

  if (co2 < 0) {
    co2 = previousCO2;
  }

  debug("Dust: " + String(dust) + "  CO2: " + String(co2) + "  Gas: " + String(mappedGas) + "  Humid: " + String(humid, 2) + "  Temp: " + String(temp, 2));

  if (client.connected()) {
    String dataBuilder = String(COMMAND_DATA_UPLOAD);
    if (humid != previousHumid || true) {
      previousHumid = humid;
      dataBuilder += " " + String(ID_HUMIDITY) + ":" + String(humid, 2);
    }
    if (temp != previousTemp || true) {
      previousTemp = temp;
      dataBuilder += " " + String(ID_TEMPERATURE) + ":" + String(temp, 2);
    }
    if (mappedGas != previousGas || true) {
      previousGas = mappedGas;
      dataBuilder += " " + String(ID_GAS) + ":" + String(mappedGas);
    }
    if (dust != previousDust || true) {
      previousDust = dust;
      dataBuilder += " " + String(ID_DUST) + ":" + String(dust);
    }
    if (co2 != previousCO2 || true) {
      previousCO2 = co2;
      dataBuilder += " " + String(ID_CO2) + ":" + String(co2);
    }
    if (dataBuilder.length() > 0) {
      if (!client.publish("nafas_data", dataBuilder.c_str())) {
        Serial.println("Gagal mengirim data ke MQTT broker!");
      }
    }
  }
}

void buzz(int duration) {
  tone(BUZZER_PIN, 1000, duration);
}

void mqttCallback(char* topic, byte* message, unsigned int length) {
  String decodedMessage;
  for (int i = 0; i < length; i++) {
    decodedMessage += (char) message[i];
  }
  String decodedTopic = String(topic);
  String targetTopic = "nafas_" + String(ESP.getEfuseMac());
  if (decodedTopic == targetTopic) {
    executeRawCommand(decodedMessage);
  }
}

#define NOTE_B0  31
#define NOTE_C1  33
#define NOTE_CS1 35
#define NOTE_D1  37
#define NOTE_DS1 39
#define NOTE_E1  41
#define NOTE_F1  44
#define NOTE_FS1 46
#define NOTE_G1  49
#define NOTE_GS1 52
#define NOTE_A1  55
#define NOTE_AS1 58
#define NOTE_B1  62
#define NOTE_C2  65
#define NOTE_CS2 69
#define NOTE_D2  73
#define NOTE_DS2 78
#define NOTE_E2  82
#define NOTE_F2  87
#define NOTE_FS2 93
#define NOTE_G2  98
#define NOTE_GS2 104
#define NOTE_A2  110
#define NOTE_AS2 117
#define NOTE_B2  123
#define NOTE_C3  131
#define NOTE_CS3 139
#define NOTE_D3  147
#define NOTE_DS3 156
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_FS3 185
#define NOTE_G3  196
#define NOTE_GS3 208
#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976
#define NOTE_C7  2093
#define NOTE_CS7 2217
#define NOTE_D7  2349
#define NOTE_DS7 2489
#define NOTE_E7  2637
#define NOTE_F7  2794
#define NOTE_FS7 2960
#define NOTE_G7  3136
#define NOTE_GS7 3322
#define NOTE_A7  3520
#define NOTE_AS7 3729
#define NOTE_B7  3951
#define NOTE_C8  4186
#define NOTE_CS8 4435
#define NOTE_D8  4699
#define NOTE_DS8 4978
#define REST      0


// change this to make the song slower or faster
int tempo = 144;

// change this to whichever pin you want to use
int buzzer = BUZZER_PIN;

// notes of the moledy followed by the duration.
// a 4 means a quarter note, 8 an eighteenth , 16 sixteenth, so on
// negative numbers are used to represent dotted notes,
// so -4 means a dotted quarter note, that is, a quarter plus an eighteenth
int melody[] = {
NOTE_E5, 4,
NOTE_B4, 8,
NOTE_C5, 8,
NOTE_D5, 4,
NOTE_C5, 8,
NOTE_B4, 8,
NOTE_A4, 4,

NOTE_A4, 8,
NOTE_C5, 8,
NOTE_E5, 4,

NOTE_D5, 8,
NOTE_C5, 8,
NOTE_B4, 4,

NOTE_B4, 8,
NOTE_C5, 8,
NOTE_D5, 4,

NOTE_E5, 4,
NOTE_C5, 4,
NOTE_A4, 4,
NOTE_A4, 8,

// loop
NOTE_A4, 8,
NOTE_B4, 8,
NOTE_C5, 8,
NOTE_D5, 8,

NOTE_F5, 4,
NOTE_G5, 8,
NOTE_A5, 8,
NOTE_A5, 8,
NOTE_G5, 8,
NOTE_F5, 8,
NOTE_E5, 3,

NOTE_C5, 8,
NOTE_E5, 4,

NOTE_D5, 8,
NOTE_C5, 8,
NOTE_B4, 4,

NOTE_B4, 8,
NOTE_C5, 8,
NOTE_D5, 4,

NOTE_E5, 4,
NOTE_C5, 4,
NOTE_A4, 4,
NOTE_A4, 8,

// loop
NOTE_A4, 8,
NOTE_B4, 8,
NOTE_C5, 8,
NOTE_D5, 8,

NOTE_F5, 4,
NOTE_G5, 8,
NOTE_A5, 8,
NOTE_A5, 8,
NOTE_G5, 8,
NOTE_F5, 8,
NOTE_E5, 3,

NOTE_C5, 8,
NOTE_E5, 4,

NOTE_D5, 8,
NOTE_C5, 8,
NOTE_B4, 4,

NOTE_B4, 8,
NOTE_C5, 8,
NOTE_D5, 4,

NOTE_E5, 4,
NOTE_C5, 4,
NOTE_A4, 4,
NOTE_A4, 4,
REST, 4,
};


// sizeof gives the number of bytes, each int value is composed of two bytes (16 bits)
// there are two values per note (pitch and duration), so for each note there are four bytes
int notes=sizeof(melody)/sizeof(melody[0])/2; 

// this calculates the duration of a whole note in ms (60s/tempo)*4 beats
int wholenote = (60000 * 4) / tempo;

void playNote() {
  int divider = melody[currentNote + 1];
  int noteDuration = 0;
  if (divider > 0) {
    noteDuration = (wholenote) / divider;
  } else if (divider > 0) {
    noteDuration = (wholenote) / abs(divider);
    noteDuration *= 1.5;
  }

  tone(buzzer, melody[currentNote], noteDuration * 0.9);
  nextNoteTime = millis() + noteDuration;
  currentNote += 2;
}

void loopBanger() {
  if (isBanger && millis() >= nextNoteTime && (wifiConnected != STATUS_CONNECTED || !client.connected())) {
    if (currentNote < notes * 2) {
      playNote();
    } else {
      currentNote = 0;
    }
  }
}

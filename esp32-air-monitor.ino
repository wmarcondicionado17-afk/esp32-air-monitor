#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SparkFun_SCD4x_Arduino_Library.h>
#include <PMS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// --- SUAS CREDENCIAIS DE WI-FI ---
const char* ssid = "FamiliaWM";
const char* password = "Cam869531";

// --- SEUS DADOS DO TELEGRAM ---
#define BOT_TOKEN "8948081019:AAH_rP5cu-K3B1z-QoUi4k5VRJYUV_jB3oo"
#define CHAT_ID "-5549411662" // (Pode ser o seu ID pessoal ou o ID negativo do grupo ex: -100xxxxxxxxxx)

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// Configuração do Display OLED (128x64)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Instância do Sensor SCD40
SCD4x scd4x;

// Configuração da Serial para o PMS5003 (Pinos RX=16, TX=17)
PMS pms(Serial2);
PMS::DATA data;

// Definição dos Pinos dos Alertas Físicos
#define PIN_LED_VERDE 25
#define PIN_LED_VERMELHO 26
#define PIN_BUZZER 27

// Temporizador para o envio periódico (3600 segundos = 3600000 milissegundos)
unsigned long ultimaMensagemPeriodica = 0;
const long intervaloPeriodico = 3600000; 

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configura pinos físicos
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // 1. INICIALIZA O WI-FI PRIMEIRO
  Serial.println();
  Serial.print("A ligar ao Wi-Fi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tentativas++;
    if (tentativas > 30) {
      Serial.println("\n[AVISO] Nao foi possivel conectar ao Wi-Fi. Continuando offline...");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[SUCESSO] Wi-Fi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }

  // 2. INICIALIZA DISPLAY E SENSORES
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  Wire.begin(21, 22);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Falha ao encontrar o Display OLED!"));
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(F("Iniciando Sistema..."));
  display.display();
  delay(2000);

  if (scd4x.begin() == false) {
    Serial.println(F("Sensor SCD40 nao detectado. Verifique o I2C!"));
  } else {
    scd4x.startPeriodicMeasurement();
    Serial.println(F("Sensor SCD40 iniciado com sucesso!"));
  }
}

void loop() {
  // Lê os dados do PMS5003
  pms.read(data);

  // Tenta ler o SCD40
  if (scd4x.readMeasurement()) {
    unsigned int co2 = scd4x.getCO2();
    float temperature = scd4x.getTemperature();
    float humidity = scd4x.getHumidity();

    bool emAlertaCritico = false;
    
    // --- VERIFICAÇÃO DE MÚLTIPLOS ALERTAS ---
    String listaAlertas = "";
    int totalAlertas = 0;

    if (co2 > 1000) {
      listaAlertas += "⚠️ CO2 Alto (" + String(co2) + " ppm)\n";
      emAlertaCritico = true;
      totalAlertas++;
    }
    if (data.PM_AE_UG_2_5 > 35) {
      listaAlertas += "⚠️ PM2.5 Alto (" + String(data.PM_AE_UG_2_5) + " ug/m3)\n";
      emAlertaCritico = true;
      totalAlertas++;
    }
    if (humidity < 30) {
      listaAlertas += "⚠️ Ar Seco (" + String(humidity, 1) + "%)\n";
      totalAlertas++;
    } else if (humidity > 70) {
      listaAlertas += "⚠️ Humidade Alta (" + String(humidity, 1) + "%)\n";
      totalAlertas++;
    }
    if (temperature > 28) {
      listaAlertas += "⚠️ Temperatura Alta (" + String(temperature, 1) + "C)\n";
      totalAlertas++;
    } else if (temperature < 18) {
      listaAlertas += "⚠️ Temperatura Baixa (" + String(temperature, 1) + "C)\n";
      totalAlertas++;
    }

    // Texto para o OLED (mostra o primeiro alerta ou "Qualidade Boa")
    String statusOled = "Qualidade: Boa";
    if (totalAlertas > 0) {
      statusOled = "Atencao: " + String(totalAlertas) + " aviso(s)";
    }

    // Atuadores Físicos (LEDs e Buzzer)
    if (emAlertaCritico) {
      digitalWrite(PIN_LED_VERMELHO, HIGH);
      digitalWrite(PIN_LED_VERDE, LOW);
      digitalWrite(PIN_BUZZER, HIGH);
    } else {
      digitalWrite(PIN_LED_VERMELHO, LOW);
      digitalWrite(PIN_LED_VERDE, HIGH);
      digitalWrite(PIN_BUZZER, LOW);
    }

    // --- ENVIO PERÓDICO PARA O TELEGRAM (A CADA 20 SEGUNDOS) ---
    if (millis() - ultimaMensagemPeriodica >= intervaloPeriodico) {
      if (WiFi.status() == WL_CONNECTED) {
        String relatorio = "📊 Relatório de Qualidade do Ar\n\n";
        relatorio += "• CO2: " + String(co2) + " ppm\n";
        relatorio += "• Temp: " + String(temperature, 1) + " C\n";
        relatorio += "• Humidade: " + String(humidity, 1) + " %\n";
        relatorio += "• PM2.5: " + String(data.PM_AE_UG_2_5) + " ug/m3\n\n";
        
        if (totalAlertas > 0) {
          relatorio += "🚨 Alertas Ativos (" + String(totalAlertas) + "):\n" + listaAlertas;
        } else {
          relatorio += "✅ Estado: Qualidade do ar ideal.";
        }

        bool enviado = bot.sendMessage(CHAT_ID, relatorio, "");
        if (enviado) {
          Serial.println("Relatório com múltiplos alertas enviado para o Telegram!");
        } else {
          Serial.println("FALHA ao enviar relatório.");
        }
      }
      ultimaMensagemPeriodica = millis(); // Reinicia a contagem dos 20 segundos
    }

    // Atualiza o Display OLED
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print(F("CO2: ")); display.print(co2); display.println(F(" ppm"));
    display.print(F("Temp: ")); display.print(temperature, 1); display.println(F(" C"));
    display.print(F("Umi: ")); display.print(humidity, 1); display.println(F(" %"));
    display.print(F("PM2.5: ")); display.print(data.PM_AE_UG_2_5); display.println(F(" ug/m3"));
    
    display.setCursor(0, 52);
    display.println(statusOled);
    display.display();

  } else {
    digitalWrite(PIN_LED_VERDE, LOW);
    digitalWrite(PIN_LED_VERMELHO, LOW);
    digitalWrite(PIN_BUZZER, LOW);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("A aguardar SCD40..."));
    display.print(F("PM2.5: ")); display.print(data.PM_AE_UG_2_5); display.println(F(" ug/m3"));
    display.display();
  }

  delay(2000);
}

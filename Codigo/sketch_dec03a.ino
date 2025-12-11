#include <Arduino.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include "DHT.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

/* LCD */
hd44780_I2Cexp lcd;

/* PINOS */
#define SERVO_PIN    26
#define BUZZER_PIN   12
#define BUTTON_PIN   27
#define DHT_PIN      25
#define LED_RED      33
#define LED_GREEN    32
#define I2C_SDA_PIN  14
#define I2C_SCL_PIN  13

/* OBJETOS PRINCIPAIS */
Servo servoMotor;
DHT dht(DHT_PIN, DHT11);

/* Estrutura dos remédios */
struct Remedio {
  String nome;
  int compartimento;
  String horarios[6];
  int qtdHorarios = 0;
  int quantidade = 0;
};
Remedio lista[7];

/* Controle do servo */
const int PWM_PARADO = 1500;
int velocidadePWM = 1600;
int tempoPorSlotMs[7] = {450,450,450,450,450,450,450};
int posicaoAtualSlot = 0;

/* LCD helper */
void lcdMsg(const String &l1, const String &l2 = "") {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(l1);
  lcd.setCursor(0,1);
  lcd.print(l2);
}

bool botaoPressionado() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    return digitalRead(BUTTON_PIN) == LOW;
  }
  return false;
}

/* Movimentação do disco */
void servoGirarMs(int pwm, int ms) { servoMotor.writeMicroseconds(pwm); delay(ms); }

void moverParaCompartimento(int alvo) {
  if (alvo < 1 || alvo > 7) return;
  if (posicaoAtualSlot == 0) posicaoAtualSlot = 1;

  servoMotor.attach(SERVO_PIN, 500, 2500);
  int steps = alvo - posicaoAtualSlot;
  if (steps < 0) steps += 7;

  for (int s = 0; s < steps; s++) {
    int idxTempo = (posicaoAtualSlot - 1 + s) % 7;
    servoGirarMs(velocidadePWM, tempoPorSlotMs[idxTempo]);
  }

  servoMotor.writeMicroseconds(PWM_PARADO);
  delay(200);
  servoMotor.detach();

  posicaoAtualSlot = alvo;
}

/* BLE */
BLEServer* pServer = nullptr;
BLEService* pService = nullptr;
BLECharacteristic* pRxCharacteristic = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;

static const char* SERVICE_UUID = "12345678-1234-1234-1234-123456789abc";
static const char* RX_UUID      = "87654321-4321-4321-4321-cba987654321";
static const char* TX_UUID      = "87654321-4321-4321-4321-cba987654322";

/* Relógio simulado */
String horaAtual = "00:00";
int dia = 1, mes = 1, ano = 2025;
int ultimoMinutoDisparado[7] = {-1,-1,-1,-1,-1,-1,-1};

/* Atualização de hora */
void atualizarDataHora() {
  static unsigned long ultimoUpdate = 0;
  if (millis() - ultimoUpdate >= 5000) {
    ultimoUpdate = millis();
    int h = horaAtual.substring(0,2).toInt();
    int m = horaAtual.substring(3).toInt();
    m++;
    if (m == 60) { m = 0; h++; }
    if (h == 24) { h = 0; dia++; }
    horaAtual = (h<10?"0":"")+String(h)+":"+(m<10?"0":"")+String(m);
    if (dia > 30) { dia = 1; mes++; if (mes > 12) { mes = 1; ano++; } }
  }
}

/* Checagem de hora */
bool horaCoincide(const String &horaRemedio, const String &horaAtualStr) {
  if (horaRemedio.length() < 5) return false;
  return horaRemedio.substring(0,5) == horaAtualStr.substring(0,5);
}

bool temHorarioAgora(Remedio &r) {
  for (int i = 0; i < r.qtdHorarios; i++)
    if (horaCoincide(r.horarios[i], horaAtual)) return true;
  return false;
}

/* Exibição alternada (principal + slots) */
unsigned long ultimaTrocaTela = 0;
bool telaPrincipal = true;
int slotIndexMostrar = 0;

void mostrarTelasAlternadas() {
  if (millis() - ultimaTrocaTela < 2200) return;
  ultimaTrocaTela = millis();
  telaPrincipal = !telaPrincipal;

  if (telaPrincipal) {
    float temp = dht.readTemperature();
    float umid = dht.readHumidity();
    if (isnan(temp) || isnan(umid)) {
      for (int k=0;k<3;k++){
        delay(30);
        temp = dht.readTemperature();
        umid = dht.readHumidity();
        if (!isnan(temp)&&!isnan(umid)) break;
      }
    }
    lcdMsg(horaAtual + " " + String(dia) + "/" + String(mes),
           "T:" + String((int)temp) + "C U:" + String((int)umid) + "%");
    return;
  }

  int checks = 0;
  while (checks < 7) {
    int i = slotIndexMostrar;
    slotIndexMostrar++;
    if (slotIndexMostrar >= 7) slotIndexMostrar = 0;
    checks++;

    if (lista[i].nome != "" && lista[i].quantidade > 0) {
      lcdMsg("S"+String(i+1)+" "+lista[i].nome,
             "Qtd: "+String(lista[i].quantidade));
      return;
    }
  }
  lcdMsg("Sem remedios","");
}

/* Notificação BLE */
void bleNotify(const String &msg) {
  if (pTxCharacteristic) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
  }
}

/* Recepção BLE */
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {

    String data = String(pChar->getValue().c_str());
    data.trim();
    if (data.length() == 0) return;

    /* Comando QTD */
    if (data.startsWith("QTD") || data.startsWith("qtd") || data.startsWith("Qtd")) {
      int p1 = data.indexOf(';');
      int p2 = data.indexOf(';', p1+1);
      if (p1 < 0 || p2 < 0) return;
      int slot = data.substring(p1+1, p2).toInt();
      int qtd  = data.substring(p2+1).toInt();
      if (slot >=1 && slot <=7) {
        lista[slot-1].quantidade = max(0, qtd);
        lcdMsg("Slot "+String(slot),"Qtd "+String(qtd));
        bleNotify("OK QTD S"+String(slot)+" = "+String(qtd));
      }
      delay(1200);
      return;
    }

    /* Cadastro nome;slot;hora */
    int p1 = data.indexOf(';');
    int p2 = data.indexOf(';', p1+1);
    if (p1 < 0 || p2 < 0) return;

    String nome = data.substring(0,p1);
    int compart = data.substring(p1+1,p2).toInt();
    String hora = data.substring(p2+1);

    if (compart < 1 || compart > 7) {
      lcdMsg("Comp invalido","");
      bleNotify("ERRO compart");
      delay(1200);
      return;
    }

    if (hora.length()==4) hora="0"+hora;

    Remedio &r = lista[compart-1];
    if (r.nome != nome) r.nome = nome;
    r.compartimento = compart;

    if (r.qtdHorarios < 6) {
      r.horarios[r.qtdHorarios++] = hora;
      lcdMsg("Horario","inserido");
      bleNotify("OK horario "+hora+" S"+String(compart));
    } else {
      lcdMsg("Limite 6 horas","");
      bleNotify("ERRO limite");
    }
    delay(1200);
  }
};

/* Setup */
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN,I2C_SCL_PIN);

  lcd.begin(16,2);
  lcd.backlight();
  lcdMsg("Dispensador ON");
  delay(1200);

  dht.begin();
  pinMode(BUZZER_PIN,OUTPUT);
  pinMode(BUTTON_PIN,INPUT_PULLUP);
  pinMode(LED_RED,OUTPUT);
  pinMode(LED_GREEN,OUTPUT);

  BLEDevice::init("Dispensador_Rem");
  pServer  = BLEDevice::createServer();
  pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  pRxCharacteristic = pService->createCharacteristic(RX_UUID,BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->start();

  lcdMsg("Pronto p/ uso");
  delay(1200);
  horaAtual = "08:00";
}

/* Loop principal */
void loop() {
  atualizarDataHora();
  mostrarTelasAlternadas();

  int minutoAtual = horaAtual.substring(3).toInt();

  for (int i = 0; i < 7; i++) {
    if (lista[i].nome != "" && temHorarioAgora(lista[i])) {
      if (ultimoMinutoDisparado[i] == minutoAtual) continue;
      ultimoMinutoDisparado[i] = minutoAtual;

      moverParaCompartimento(i+1);
      digitalWrite(LED_RED,HIGH);
      lcdMsg("Tomar:",lista[i].nome);

      while (!botaoPressionado()) {
        tone(BUZZER_PIN,1200,200); delay(250);
        tone(BUZZER_PIN,800,200); delay(250);
      }

      noTone(BUZZER_PIN);
      digitalWrite(LED_RED,LOW);

      if (lista[i].quantidade > 0) lista[i].quantidade--;

      digitalWrite(LED_GREEN,HIGH);
      lcdMsg("Retirado!","Restam "+String(lista[i].quantidade));
      bleNotify("OK retirada S"+String(i+1)+" restam "+String(lista[i].quantidade));
      delay(2000);
      digitalWrite(LED_GREEN,LOW);
    }
  }

  delay(50);
}

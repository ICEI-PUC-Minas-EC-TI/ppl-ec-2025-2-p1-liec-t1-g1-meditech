# MedTech

`LOURDES`

`ENGENHARIA DE COMPUTAÇÃO`

`1º SEMESTRE`

`LABORATORIO DE INTRODUÇÃO A ENGENHARIA DE COMPUTAÇÃO`


## Integrantes

* Diego Henrique Alves da Silva
* Gabriel Mesquita da Silva


## Orientador

* Dr. Felipe Augusto Lara Soares

## Resumo

O Dispensador de Remédios com ESP32 é um sistema automatizado para auxiliar na administração de medicamentos em horários programados. Ele utiliza um ESP32 integrado a um servo motor, LCD 16x2 (I2C), sensor DHT11, buzzer, LEDs e um botão de confirmação. A configuração dos horários e quantidades é feita via Bluetooth Low Energy (BLE), permitindo que o usuário ou cuidador cadastre remédios e ajuste doses diretamente pelo celular, sem necessidade de Wi-Fi.
O dispositivo exibe no LCD informações como hora/data, temperatura/umidade e status dos slots (nome do remédio e quantidade restante). Quando chega o horário de um medicamento, o servo posiciona o compartimento correto, aciona alerta sonoro e visual, e aguarda a confirmação pelo botão. Após a retirada, o sistema decrementa a quantidade e envia uma notificação via BLE.

# Código (do arduino ou esp32)

<li><a href="Codigo/README.md">#include <Arduino.h>
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
} (.ino)</a></li>

# Aplicativo para Smartphone

<li><a href="App/README.md"> Aplicativo </a></li>

# Apresentação

<ol>
<li><a href="Apresentacao/README.md"> https://drive.google.com/file/d/1E1oTtEv5x9CtwY4NwOVhtHTfC4Undxda/view?usp=drive_link</a></li>
<li><a href="Apresentacao/README.md"> https://drive.google.com/file/d/1E1oTtEv5x9CtwY4NwOVhtHTfC4Undxda/view?usp=drive_link</a></li>
</ol>

# Manual de Utilização

<li><a href="Manual/manual de utilização.md"> 
# Dispensador de Remédios com ESP32 — Guia de Utilização

> Projeto do Diego Henrique Alves Da Silva & Gabriel Mesquita da Silva

## 1) Visão geral
- Controle de servo de rotação contínua para posicionar 7 slots.
- LCD 16x2 (I2C) alternando telas de Hora/Data + Temperatura/Umidade(DHT11) e Resumo dos slots (nome + quantidade).
- BLE para cadastrar horários(de cada slot) e ajustar quantidades total.
- Alerta por LED vermelho e buzzer na hora do remédio; confirmação feita por um botão; depois é feito o decremento automático da quantidade.
- Relógio simulado: avança 1 min real =  5seg.

## 2) Materiais (BOM)
- ESP32 DevKit
- Servo de rotação contínua (5 V)
- LCD 16x2 com interface I2C (módulo PCF8574)
- Sensor DHT11 (temperatura/umidade)
- Buzzer (ativo ou passivo)
- Botão (push-button) — entrada com `INPUT_PULLUP`
- LEDs: Vermelho (alerta) e verde (confirmação)
- Resistores para LEDs
- Cabos/jumpers e protoboard


## 3) Ligações elétricas

 Servo (sinal) → GPIO 26 (SERVO_PIN)  
 Buzzer → GPIO 12 (BUZZER_PIN)  
 Botão → GPIO 27 (BUTTON_PIN)  lado no GND e o outro no GPIO 27
 DHT11 → GPIO 25 (DHT_PIN)  
 LED vermelho → GPIO 33 (LED_RED) + resistor
 LED verde → GPIO 32 (LED_GREEN) + resistor 
 LCD I2C → SDA = GPIO 14 (I2C_SDA_PIN), SCL = GPIO 13 (I2C_SCL_PIN)  

## 4) Instalação de software

### 4.1. Arduino IDE + ESP32
- Instale Arduino IDE.
- No Boards Manager, instale ESP32 by Espressif Systems.
- Placa: ESP32 Dev Module/Correspondente.

### 4.2. Bibliotecas
Instale pelo Library Manager:
- ESP32Servo
- DHT sensor library (Adafruit) + Adafruit Unified Sensor
- hd44780 (Bill Perry) 
- ESP32 BLE Arduino

### 4.2. Upload
- Sketch no Arduino IDE.
- Selecione Placa e Porta corretas.
- Faça Upload.  

## 5) BLE — Comandos
### 5.1. Identificação
- Nome do dispositivo BLE: Dispensador_Rem

### 5.2. UUIDs (do código)
- Service: 12345678-1234-1234-1234-123456789abc
- RX (Write / Write_NR): 87654321-4321-4321-4321-cba987654321
- TX (Notify): 87654321-4321-4321-4321-cba987654322

### 5.3. Apps recomendados
**nRF Connect**
- Abra, faça Scan e conecte em Dispensador_Rem.
- Entre no serviço (12345678-...).
- Enable Notifications na característica TX (...4322).
- Write na RX (...4321) para enviar comandos.

**Serial Bluetooth Terminal (USADO)**
- Na aba Devices, faça clique longo em Dispensador_Rem → Define custom profile.
   - Service: 12345678-1234-1234-1234-123456789abc
   - TX (Notify): 87654321-4321-4321-4321-cba987654322
   - RX (Write/Write_NR): 87654321-4321-4321-4321-cba987654321
3. Conecte, ative Notify e envie comandos.

### 6.4. Comandos suportados
-Cadastrar horário que vai tomar o remedio (máx. 6 por slot)
<nome>;<slot>;<HH:MM>
Ex: Dipirona;7;10:30

-Ajustar quantidade do slot
QTD;<slot>;<quantidade>
Ex: QTD;5;8

**Respostas (Notify)**
- Sucesso horário: OK horario 08:00 S5
- Sucesso quantidade: OK QTD S5=8
- Retirada: OK retirada S5 restam X


## 7) LCD
O display alterna a cada 2 segundos
1. Hora/Data  + Temperatura/Umidade  
   `08:15 10/12`  
   `T:27C U:52%`
2. Slot com remédio (se houver quantidade > 0):  
   `S5 dipirona`  
   `Qtd:8`
Se não houver, aparece `Sem remedios`


## 8) Funcionamento
- Quando a horaAtual coincide com algum horário que você colocou:
  1. Servo gira até o slot.
  2. LED vermelho acende; buzzer toca.
  3. LCD mostra: `Tomar: <nome>`.
  4. Pressione o botão para confirmar.
  5. O buzzer desliga, apaga LED vermelho, acende LED verde, decrementa 1 da quantidade (se > 0), envia Notify e mostra Retirado + Restam X.



## 9) Próximos passos 
- Adicionar Preferences (NVS) para salvar configurações.
- Fazer um aplicativo mobile proprio.
- Fazer um projeto 3d para o trabalho.



**Autor:** Diego Henrique Alves Da Silva & Gabriel Mesquita da Silva


</a></li>


# Documentação

<ol>
<li><a href="Documentacao/01-Introducão.md"> O projeto Dispensador de Remédios com ESP32 visa automatizar a liberação de medicamentos em horários pré-definidos, utilizando conectividade BLE para configuração remota, alertas visuais e sonoros, e exibição de informações em LCD. O contexto envolve a necessidade de melhorar a adesão terapêutica e reduzir erros na administração de medicamentos. O público-alvo inclui pacientes, cuidadores e clínicas que necessitam de uma solução simples e confiável.

#Problema# Muitos pacientes esquecem de tomar medicamentos nos horários corretos, o que compromete os tratamentos e ocasionando em situações complicadas. Em ambientes com múltiplos pacientes , a falta de controle aumenta os riscos ou pessoas que tomam remédios ou que tem problemas de esquecimento acabam não tomando os medicamentos. O problema é garantir alertas precisos, identificação do remédio e registro da retirada, sem depender de Wi-Fi e com interface simples.

#Objetivos# Objetivo Geral: Desenvolver um dispositivo embarcado que avise, posicione e registre a retirada de medicamentos de forma segura e configurável via BLE. Objetivos Específicos: - Implementar mecanismo de posicionamento via servo para 7 slots. - Criar interface BLE para cadastrar horários e ajustar quantidades. - Exibir hora/data, temperatura/umidade e resumo dos slots no LCD. - Implementar alerta sonoro/visual e confirmação por botão.

#Público-Alvo# O projeto é destinado a pessoas que necessitam seguir horários rigorosos de medicação, bem como cuidadores e clínicas que buscam maior controle na administração de medicamentos. O perfil dos usuários inclui indivíduos com pouca experiência tecnológica, garantindo acessibilidade por meio de uma interface simples e intuitiva. Tanto o aplicativo quanto o dispositivo foram projetados para serem fáceis de compreender e operar, mesmo por usuários sem conhecimentos avançados em tecnologia.</a></li>
<li><a href="Documentacao/02-Metodologias Ágeis.md"> O projeto Dispensador de Remédios com ESP32 visa automatizar a liberação de medicamentos em horários pré-definidos, utilizando conectividade BLE para configuração remota, alertas visuais e sonoros, e exibição de informações em LCD. O contexto envolve a necessidade de melhorar a adesão terapêutica e reduzir erros na administração de medicamentos. O público-alvo inclui pacientes, cuidadores e clínicas que necessitam de uma solução simples e confiável.

#Problema# Muitos pacientes esquecem de tomar medicamentos nos horários corretos, o que compromete os tratamentos e ocasionando em situações complicadas. Em ambientes com múltiplos pacientes , a falta de controle aumenta os riscos ou pessoas que tomam remédios ou que tem problemas de esquecimento acabam não tomando os medicamentos. O problema é garantir alertas precisos, identificação do remédio e registro da retirada, sem depender de Wi-Fi e com interface simples.

#Objetivos# Objetivo Geral: Desenvolver um dispositivo embarcado que avise, posicione e registre a retirada de medicamentos de forma segura e configurável via BLE. Objetivos Específicos: - Implementar mecanismo de posicionamento via servo para 7 slots. - Criar interface BLE para cadastrar horários e ajustar quantidades. - Exibir hora/data, temperatura/umidade e resumo dos slots no LCD. - Implementar alerta sonoro/visual e confirmação por botão.

#Público-Alvo# O projeto é destinado a pessoas que necessitam seguir horários rigorosos de medicação, bem como cuidadores e clínicas que buscam maior controle na administração de medicamentos. O perfil dos usuários inclui indivíduos com pouca experiência tecnológica, garantindo acessibilidade por meio de uma interface simples e intuitiva. Tanto o aplicativo quanto o dispositivo foram projetados para serem fáceis de compreender e operar, mesmo por usuários sem conhecimentos avançados em tecnologia.</a></li>
<li><a href="Documentacao/03-Desenvolvimento.md"> Materiais
Os materiais utilizados no projeto foram:

ESP32 DevKit, Servo de rotação contínua, LCD 16x2 com I2C, Sensor DHT11, Buzzer, Botão, LEDs (vermelho e verde) + resistores, Jumpers e protoboard.
Desenvolvimento
Descreva aqui como foi o desenvolvimento do trabalho, destacando cada uma das etapas necessárias para chegar até a solução final.

Desenvolvimento do Aplicativo
Interface
Interface BLE genérica (nRF Connect ou Serial Bluetooth Terminal). LCD alterna entre hora/data + T/UR e slots com quantidade.

Código
BLE com Service UUID e duas características (RX Write, TX Notify). Comandos: nome;slot;HH:MM e QTD;slot;qtd. Respostas via Notify.

Desenvolvimento do Hardware
Montagem
Montagem conforme pinos definidos no código.

Desenvolvimento do Código
O desenvolvimento do código foi realizado passo a passo: primeiro criamos um código individual para cada componente do projeto e, em seguida, fizemos a integração de todos eles, realizando os ajustes necessários para que tudo funcionasse de forma unificada.

Comunicação entre App e Hardware
BLE GATT: app escreve na RX e recebe Notify na TX. Protocolo simples em texto.

Descreva como foi o processo de comunicação entre App e arduino/ESP. </a></li>
<li><a href="Documentacao/04-Testes.md"> Foram realizados testes de conexão BLE, envio de comandos para configuração de horários e ajuste de quantidades, além da verificação do alerta no horário simulado. Também foi testada a confirmação por botão e o decremento automático da quantidade após a retirada. Houve tentativa de desenvolver um aplicativo no App Inventor, porém não foi possível concluir a integração BLE. Optou-se pelo uso de ferramentas como nRF Connect e Serial Bluetooth Terminal, que se mostraram eficazes para testes e configuração. Resultados: O funcionamento geral foi satisfatório, com alertas corretos e comunicação BLE estável. Limitações: Relógio simulado, dados voláteis, necessidade de calibração manual do servo para cada slot. </a></li>
<li><a href="Documentacao/05-Conclusão.md"> Conclusão  O projeto demonstrou ser uma solução viável para auxiliar o público-alvo, oferecendo alertas sonoros e visuais, controle automatizado dos compartimentos e interface simples via BLE para configuração. A implementação atingiu os objetivos principais, garantindo facilidade de uso e funcionamento adequado em testes práticos. Apesar dos resultados positivos, foram identificadas limitações que podem ser abordadas em versões futuras, como a ausência de persistência dos dados após reinicialização, dependência de relógio simulado e necessidade de calibração manual do servo. Além disso, a integração com um aplicativo dedicado não foi concluída, sendo substituída por ferramentas genéricas como nRF Connect.

 </a></li>
<li><a href="Documentacao/06-Referências.md"> Referências
-PERRY, Bill. hd44780 Library: Documentação oficial. Disponível em: https://github.com/duinoWitchery/hd44780. Acesso em: 05 dez. 2025. -ADAFRUIT. DHT Sensor Library. Disponível em: https://github.com/adafruit/DHT-sensor-library. Acesso em: 05 dez. 2025. HARRINGTON, Kevin; BENNETT, John K. ESP32Servo Library. Disponível em: https://github.com/madhephaestus/ESP32Servo. Acesso em: 05 dez. 2025. -KOLBAN, Neil. ESP32 BLE Arduino. Disponível em: https://github.com/nkolban/ESP32_BLE_Arduino. Acesso em: 05 dez. 2025. -ESPRESSIF SYSTEMS. ESP32 Arduino Core. Disponível em: https://github.com/espressif/arduino-esp32. Acesso em: 05 dez. 2025. -DATASHEET. DHT11 – Temperature & Humidity Sensor. Disponível em: https://www.adafruit.com/product/386. Acesso em: 05 dez. 2025. -DATASHEET. PCF8574 – Remote 8-bit I/O expander for I2C-bus. Disponível em: https://www.nxp.com/docs/en/data-sheet/PCF8574.pdf. Acesso em: 05 dez. 2025. -SOARES, Felipe Augusto Lara. Microcontroladores – Comunicação Bluetooth. [s.l.], [s.d.]. Apresentação em PowerPoint convertida para PDF. Material fornecido pelo professor. Acesso em: 05 dez. 2025. -SOARES, Felipe Augusto Lara. Tutorial APPinventor – Relatório 5. [s.l.], [s.d.]. Documento em Word convertido para PDF. Material fornecido pelo professor. Acesso em: 05 dez. 2025. </a></li>
</ol>


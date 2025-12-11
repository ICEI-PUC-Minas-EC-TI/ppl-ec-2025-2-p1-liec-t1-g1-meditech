
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



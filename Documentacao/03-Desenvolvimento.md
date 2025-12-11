
# Materiais

Os materiais utilizados no projeto foram:
- ESP32 DevKit, Servo de rotação contínua, LCD 16x2 com I2C, Sensor DHT11, Buzzer, Botão, LEDs (vermelho e verde) + resistores, Jumpers e protoboard.

# Desenvolvimento

Descreva aqui como foi o desenvolvimento do trabalho, destacando cada uma das etapas necessárias para chegar até a solução final.

## Desenvolvimento do Aplicativo

### Interface

Interface BLE genérica (nRF Connect ou Serial Bluetooth Terminal). LCD alterna entre hora/data + T/UR e slots com quantidade.

### Código

BLE com Service UUID e duas características (RX Write, TX Notify). Comandos: nome;slot;HH:MM e QTD;slot;qtd. Respostas via Notify.

## Desenvolvimento do Hardware

### Montagem

Montagem conforme pinos definidos no código.

### Desenvolvimento do Código

O desenvolvimento do código foi realizado passo a passo: primeiro criamos um código individual para cada componente do projeto e, em seguida, fizemos a integração de todos eles, realizando os ajustes necessários para que tudo funcionasse de forma unificada.

## Comunicação entre App e Hardware

BLE GATT: app escreve na RX e recebe Notify na TX. Protocolo simples em texto.

Descreva como foi o processo de comunicação entre App e arduino/ESP.

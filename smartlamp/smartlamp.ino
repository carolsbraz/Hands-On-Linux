#include <DHT.h>
#include <Arduino.h>
#include <analogWrite.h>

// Defina os pinos de LED e LDR
const int ledPin = 15; // Por exemplo, use o pino 2 para o LED
const int ldrPin = 36; // Por exemplo, use o pino 34 para o LDR (analog)
const int dhtPin = 4; // Por exemplo, use o pino 4 para o DHT11

#define DHTTYPE DHT11

DHT dht(dhtPin, DHTTYPE); // Inicialize o objeto DHT

// Defina uma variável com valor máximo do LDR (4000)
const int ldrMax = 4000;

// Defina uma variável para guardar o valor atual do LED (10)
int ledValue = 10;

void setup() {
    Serial.begin(9600);
    
    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);

    dht.begin();

    Serial.println("SmartLamp Initialized.");
    processCommand("GET_LDR\n");

}

void loop() {
    // Obtenha os comandos enviados pela serial e processe-os com a função processCommand
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        processCommand(command);
    }
}

void processCommand(String command) {
    // compare o comando com os comandos possíveis e execute a ação correspondente      
    if (command.equals(String("GET_LDR"))) {
      Serial.printf("RES GET_LDR %d\n", ldrGetValue());
    } else if (command.equals(String("GET_LED"))) {
       Serial.printf("RES GET_LED %d\n", ledValue);
    } else if (command.substring(0, 7).equals(String("SET_LED"))) {
      int set_led_value = command.substring(7).toInt();  
      if ( set_led_value >= 0 && set_led_value <=100) {
        ledUpdate(set_led_value);
        Serial.printf("RES SET_LED 1\n");
      } else {
        Serial.printf("RES SET_LED -1\n");
      }
    } else if (command.equals(String("GET_TEMP"))) {
        float temp = dht.readTemperature();
        Serial.print("RES GET_TEMP ");
        Serial.print(temp);
        Serial.print("\n");
    } else if (command.equals(String("GET_HUM"))) {
        float hum = dht.readHumidity();
        Serial.print("RES GET_HUM ");
        Serial.print(hum);
        Serial.print("\n");
    } else {
      Serial.printf("ERR Unknown command.");
    } 
}

void ledUpdate(int value) {
    // Normalize o valor do LED antes de enviar para a porta correspondente
    ledValue = value;  // Atualiza o valor global do LED
    int ledBrightness = map(ledValue, 0, 100, 0, 255); // Normaliza para a faixa do PWM do ESP32
    analogWrite(ledPin, ledBrightness);
}


int ldrGetValue() {
    // Leia o sensor LDR e retorne o valor normalizado
    int ldrValue = analogRead(ldrPin);
    return ldrValue;
}

#include <DHT.h>

// Defina os pinos de LED e LDR
const int ledPin = 2; // Por exemplo, use o pino 2 para o LED
const int ldrPin = 34; // Por exemplo, use o pino 34 para o LDR (analog)
const int dhtPin = 4; // Por exemplo, use o pino 4 para o DHT11

#define DHTTYPE DHT11

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
}

void loop() {
    // Obtenha os comandos enviados pela serial e processe-os com a função processCommand
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        processCommand(command);
    }
}

void processCommand(String command) {
    // Compare o comando com os comandos possíveis e execute a ação correspondente
    if (command == "LED_ON") {
        ledValue = 255; // Defina o valor máximo para ligar o LED completamente
        ledUpdate();
    } else if (command == "LED_OFF") {
        ledValue = 0; // Desligue o LED completamente
        ledUpdate();
    } else if (command == "GET_TEMP") {
        float temp = dht.readTemperature();
        Serial.print("Temperature: ");
        Serial.println(temp);
    } else if (command == "GET_HUM") {
        float hum = dht.readHumidity();
        Serial.print("Humidity: ");
        Serial.println(hum);
    }
}

void ledUpdate() {
    // Normalize o valor do LED antes de enviar para a porta correspondente
    int ledBrightness = map(ledValue, 0, 255, 0, 1023); // Normaliza o valor para a faixa do PWM do ESP32
    analogWrite(ledPin, ledBrightness);
}

int ldrGetValue() {
    // Leia o sensor LDR e retorne o valor normalizado
    int ldrValue = analogRead(ldrPin);
    return ldrValue;
}

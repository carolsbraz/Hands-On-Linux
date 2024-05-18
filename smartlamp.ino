// Defina os pinos de LED e LDR
int ledPin = 16;
int ldrPin = 4;

// Defina uma variável com valor máximo do LDR (4000)
int ldrMax = 4000;

// Defina uma variável para guardar o valor atual do LED (10)
int ledValue = 10;

void setup() {
    Serial.begin(9600);
    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);
    
    // Inicializa o LED com o valor padrão
    ledUpdate();
    
    Serial.printf("SmartLamp Initialized.\n");

    processCommand("GET_LDR");

}

// Função loop será executada infinitamente pelo ESP32
void loop() {
    // Obtenha os comandos enviados pela serial 
    // e processe-os com a função processCommand
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        processCommand(command);
    }
}

void processCommand(String command) {
    command.trim();
    
    if (command.startsWith("SET_LED ")) {
        int intensity = command.substring(8).toInt();
        if (intensity >= 0 && intensity <= 100) {
            ledValue = intensity;
            ledUpdate();
            Serial.println("RES SET_LED 1");
        } else {
            Serial.println("RES SET_LED -1");
        }
    } else if (command.equals("GET_LED")) {
        Serial.printf("RES GET_LED %d\n", ledValue);
    } else if (command.equals("GET_LDR")) {
        int ldrValue = ldrGetValue();
        Serial.printf("RES GET_LDR %d\n", ldrValue);
    } else {
        Serial.println("ERR Unknown command.");
    }
}

// Função para atualizar o valor do LED
void ledUpdate() {
    int normalizedValue = map(ledValue, 0, 100, 0, 255);
    analogWrite(ledPin, normalizedValue);
}

// Função para ler o valor do LDR
int ldrGetValue() {
    int ldrValue = analogRead(ldrPin);
    int normalizedLdrValue = map(ldrValue, 0, ldrMax, 0, 100);
    return normalizedLdrValue;
}

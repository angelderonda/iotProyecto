#include <MFRC522.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

// Definir los pines del lector RFID
#define RST_PIN D1
#define SS_PIN D8
#define LED_BUILTIN D0
#define BUTTON_PIN D3
#define PIR_PIN D2


MFRC522 mfrc522(SS_PIN, RST_PIN);  // Instancia de la clase

// Definir los detalles de la conexión a WiFi y MQTT
const char* ssid = "Angelito";
const char* password = "c@lvo123";
const char* mqtt_server = "192.168.118.88";  //mosquitto_sub -h 192.168.118.88 -t \# -d

// Nombres de los tópicos MQTT
const char* mqtt_topic_solicitud = "solicitud/NFC";
const char* mqtt_topic_comando = "cmd/abrir";
const char* mqtt_topic_max_intentos = "maxIntentos";
const char* mqtt_topic_timbre = "solicitud/timbre";
const char* mqtt_topic_alarma = "alarma";
const char* mqtt_topic_configuracion = "cmd/conf";



// Crear una instancia del cliente MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool isCardReadEnabled = true;              // Variable para habilitar/deshabilitar la lectura de tarjetas
bool isDoorOpen = false;                    // Variable para indicar si la puerta está abierta
bool isReadModeEnabled = true;              // Variable para habilitar/deshabilitar el modo de lectura de tarjetas
unsigned int failedAttempts = -1;           // Contador de intentos fallidos de lectura de tarjetas
unsigned int maxAttempts = 5;               // Número máximo de intentos fallidos permitidos
unsigned long tiempoOpen = 3000;            // Variable para controlar el tiempo de encendido del led 3 segundos predeterminado
bool buttonState = false;                   //Estado del pulsador
bool isMotionDetected = false;              // Movimiento del sensor PIR
bool alarma = true;                        // Alarma encendida por defecto
unsigned long motionStartTime = 0;          // Variable para almacenar el tiempo de inicio de detección de movimiento
const unsigned long motionDuration = 7000;  // Duración mínima de detección de movimiento en milisegundos (7 segundos)
unsigned long lastCardReadTime = 0;         // Variable para almacenar el tiempo de la última lectura de tarjeta
unsigned long cardReadInterval = 5000;      // Intervalo de tiempo en milisegundos entre lecturas de tarjetas

void reconnect();
void handleButtonPress();
void handleCardRead();
void processCommand(const char* topic, const char* message);
void resetFailedAttempts();
void updateAttempts();
void enableReadMode();
void disableReadMode();

void setup() {
  // Inicializar el lector RFID
  SPI.begin();
  mfrc522.PCD_Init();

  // Inicializar la conexión serial
  Serial.begin(9600);

  // Conectar al WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Conectado al WiFi");

  // Conectar al broker MQTT
  mqttClient.setServer(mqtt_server, 1883);
  // Convertir la función processCommand en un puntero a función compatible
  std::function<void(char*, byte*, unsigned int)> callbackWrapper = [](char* topic, byte* payload, unsigned int length) {
    callback(topic, payload, length);
  };

  // Setear el callback del cliente MQTT
  mqttClient.setCallback(callbackWrapper);
  reconnect();

  // Nos suscribimos a la configuracion
  mqttClient.subscribe(mqtt_topic_comando);
  mqttClient.subscribe(mqtt_topic_configuracion);


  // Configurar el pin del LED incorporado como salida
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Apagar el LED al inicio

  // Configurar el pulsador
  pinMode(BUTTON_PIN, INPUT);

  // Configurar la función de reinicio del contador de intentos fallidos
  resetFailedAttempts();

  // Actualizar el valor de intentosFallidos
  updateAttempts();

  // Habilitar el modo de lectura de tarjetas
  enableReadMode();
}


void loop() {
  handleCardRead();
  delay(10);
  handleButtonPress();
  delay(10);
  detectMotion();
  delay(10);
  mqttClient.loop();
  delay(10);
}

void handleButtonPress() {
  if (digitalRead(BUTTON_PIN) == LOW && !buttonState) {
    buttonState = true;
    mqttClient.publish(mqtt_topic_timbre, "Timbre pulsado");
  } else if (digitalRead(BUTTON_PIN) == HIGH) {
    buttonState = false;
  }
}

void handleCardRead() {
  if (isCardReadEnabled && isReadModeEnabled && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    isCardReadEnabled = false;
    lastCardReadTime = millis();

    String cardNumber = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      cardNumber.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
      cardNumber.concat(String(mfrc522.uid.uidByte[i], HEX));
    }
    Serial.println("Número de tarjeta leído: " + cardNumber);

    StaticJsonDocument<200> jsonDocument;
    jsonDocument["tag"] = cardNumber;

    String jsonString;
    serializeJson(jsonDocument, jsonString);

    mqttClient.publish(mqtt_topic_solicitud, jsonString.c_str());
  }

  if (!isCardReadEnabled && (millis() - lastCardReadTime >= cardReadInterval)) {
    isCardReadEnabled = true;
  }
}

void reconnect() {
  while (!mqttClient.connected()) {
    Serial.println("Intentando reconexión al broker MQTT...");
    if (mqttClient.connect("arduino")) {
      Serial.println("Conectado al broker MQTT");
    } else {
      Serial.println("Falló al conectar al broker MQTT. Reintentando en 5 segundos...");
      delay(5000);
    }
  }
}

void detectMotion() {
  if (digitalRead(PIR_PIN) == HIGH) {
    //Serial.println(" MOVIMIENTO...");
    if (!isMotionDetected) {
      motionStartTime = millis();  // Actualizar el tiempo de inicio de detección de movimiento
      isMotionDetected = true;
    }
  } else {
    motionStartTime = 0;  // Actualizar el tiempo de inicio de detección de movimiento

    if (isMotionDetected && (millis() - motionStartTime >= motionDuration) && alarma) {

      //Serial.println("Movimiento detectado durante al menos 7 segundos");

      mqttClient.publish(mqtt_topic_alarma, "Movimiento detectado, cuidado!!");
    }

    isMotionDetected = false;
  }
}


void processCommand(const char* topic, const char* message) {
  Serial.print("Mensaje recibido en el tópico: ");
  Serial.println(topic);

  String receivedMessage(message);

  if (strcmp(topic, mqtt_topic_comando) == 0) {
    DynamicJsonDocument jsonDocument(200);
    DeserializationError error = deserializeJson(jsonDocument, receivedMessage);
    if (error) {
      Serial.println("Error al analizar el JSON");
      return;
    }

    if (jsonDocument.containsKey("OK") && jsonDocument["OK"] == true) {
      Serial.println("Puerta abierta");
      resetFailedAttempts();
      isDoorOpen = true;
      digitalWrite(LED_BUILTIN, LOW);
      delay(tiempoOpen);
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      Serial.println("Intento fallido...");
      isDoorOpen = false;
      for (int i = 0; i < 10; i++) {
        digitalWrite(LED_BUILTIN, LOW);
        delay(100);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
      }
      updateAttempts();
    }
  } else if (strcmp(topic, mqtt_topic_configuracion) == 0) {

    Serial.println("Cambiando configuración...");    

    DynamicJsonDocument jsonDocument(200);
    DeserializationError error = deserializeJson(jsonDocument, receivedMessage);
    if (error) {
      Serial.println("Error al analizar el JSON");
      return;
    }


    if (jsonDocument.containsKey("alarma")) {
      alarma = jsonDocument["alarma"];
      Serial.print("Nueva configuración de alarma: ");
      Serial.println(alarma);
    }

    if (jsonDocument.containsKey("tiempoOpen")) {
      tiempoOpen = long(jsonDocument["tiempoOpen"])*1000;
      Serial.print("Nueva configuración de tiempoOpen: ");
      Serial.println(tiempoOpen);
    }

    if (jsonDocument.containsKey("cardReadInterval")) {
      cardReadInterval = long(jsonDocument["cardReadInterval"])*1000;
      Serial.print("Nueva configuración de cardReadInterval: ");
      Serial.println(cardReadInterval);
    }

    if (jsonDocument.containsKey("isReadModeEnabled")) {
      isReadModeEnabled = jsonDocument["isReadModeEnabled"];
      Serial.print("Nueva configuración de isReadModeEnabled: ");
      Serial.println(isReadModeEnabled);
    }

    if (jsonDocument.containsKey("maxAttempts")) {
      maxAttempts = jsonDocument["maxAttempts"];
      Serial.print("Nueva configuración de maxAttempts: ");
      Serial.println(maxAttempts);
    }
  }
}

void resetFailedAttempts() {
  failedAttempts = 0;
}

void updateAttempts() {
  failedAttempts += 1;

  if (failedAttempts >= maxAttempts) {
    disableReadMode();
  }
}

void enableReadMode() {
  isReadModeEnabled = true;
}

void disableReadMode() {
  isReadModeEnabled = false;

  StaticJsonDocument<50> jsonDocument;
  jsonDocument["maxIntentos"] = failedAttempts;

  String jsonString;
  serializeJson(jsonDocument, jsonString);

  mqttClient.publish(mqtt_topic_max_intentos, jsonString.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Esta función se llama automáticamente cuando se recibe un mensaje MQTT en el tópico suscrito

  Serial.print("Mensaje recibido en el tópico: ");
  Serial.println(topic);

  // Convertir el payload a un string
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Mensaje recibido: ");
  Serial.println(message);

  // Llamar a la función processCommand con los argumentos convertidos
  processCommand(topic, message.c_str());
}

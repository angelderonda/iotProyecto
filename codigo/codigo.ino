#include <MFRC522.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>

// Definir los pines del lector RFID
#define RST_PIN D1
#define SS_PIN D8
#define LED_BUILTIN D0
MFRC522 mfrc522(SS_PIN, RST_PIN); // Instancia de la clase

// Definir los detalles de la conexión a WiFi y MQTT
const char* ssid = "Angelito";
const char* password = "c@lvo123";
const char* mqtt_server = "192.168.118.88"; //mosquitto_sub -h 192.168.118.88 -t \# -d
const char* mqtt_topic = "topic/ejemplo";

// Crear una instancia del cliente MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool isCardReadEnabled = true;    // Variable para habilitar/deshabilitar la lectura de tarjetas
unsigned long lastCardReadTime = 0;  // Variable para almacenar el tiempo de la última lectura de tarjeta
const unsigned long cardReadInterval = 5000; // Intervalo de tiempo en milisegundos entre lecturas de tarjetas

void reconnect() {
  // Reconectar al broker MQTT
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
  mqttClient.setCallback(callback);
  while (!mqttClient.connected()) {
    Serial.println("Conectando al broker MQTT...");
    if (mqttClient.connect("arduino")) {
      Serial.println("Conectado al broker MQTT");
    } else {
      Serial.println("Falló al conectar al broker MQTT. Reintentando en 5 segundos...");
      delay(5000);
    }
  }

  // Suscribirse al tópico MQTT
  mqttClient.subscribe(mqtt_topic);

  // Configurar el pin del LED incorporado como salida
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Apagar el LED al inicio
}

void loop() {
  // Verificar si se ha detectado una tarjeta RFID
  if (isCardReadEnabled && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    // Deshabilitar la lectura de tarjetas durante 5 segundos
    isCardReadEnabled = false;
    lastCardReadTime = millis();

    // Leer el número de la tarjeta RFID
    String cardNumber = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      cardNumber.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
      cardNumber.concat(String(mfrc522.uid.uidByte[i], HEX));
    }
    Serial.println("Número de tarjeta leído: " + cardNumber);

    // Encender el LED durante un segundo
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
    digitalWrite(LED_BUILTIN, HIGH);

    // Enviar el número de la tarjeta RFID al broker MQTT
    mqttClient.publish(mqtt_topic, cardNumber.c_str());
  }

  // Verificar si ha pasado el tiempo de espera para habilitar la lectura de tarjetas nuevamente
  if (!isCardReadEnabled && (millis() - lastCardReadTime >= cardReadInterval)) {
    isCardReadEnabled = true;
  }

  // Mantener la conexión MQTT activa
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();
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

  // Realizar alguna acción dependiendo del mensaje recibido
  if (message == "reiniciar") {
    Serial.println("Reiniciando el Arduino...");
    delay(1000);
    ESP.restart();
  }
}

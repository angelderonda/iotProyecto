#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Configuración de WiFi
const char* ssid = "DIGIFIBRA-K6Ck";
const char* password = "YK7RDdeED6";

// Configuración de MQTT
const char* mqttServer = "localhost"; // Dirección del servidor MQTT
const int mqttPort = 1883; // Puerto del servidor MQTT

// Pin RX y TX para la comunicación con el módulo ESP8266
const int rxPin = 0; 
const int txPin = 1; 

SoftwareSerial espSerial(rxPin, txPin); // Crea un objeto de comunicación serie con el ESP8266
WiFiClient espClient; // Crea un objeto de cliente WiFi
PubSubClient client(espClient); // Crea un objeto de cliente MQTT

void setup() {
  Serial.begin(9600); // Inicializa la comunicación serie con la computadora
  espSerial.begin(115200); // Inicializa la comunicación serie con el ESP8266

  WiFi.begin(ssid, password); // Conéctate al WiFi

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.println("Dirección IP: " + WiFi.localIP().toString());

  client.setServer(mqttServer, mqttPort); // Configura el servidor MQTT
  //client.setCallback(callback); // Si deseas usar una función de devolución de llamada para procesar mensajes entrantes

  if (client.connect("arduinoClient")) { // Conéctate al servidor MQTT
    Serial.println("Conexión MQTT exitosa");
    //client.subscribe("topic"); // Suscríbete a un tema MQTT (opcional)
  } else {
    Serial.println("Error de conexión MQTT");
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Mantén la conexión MQTT activa

  // Envía un mensaje MQTT
  String message = "Hola desde Arduino!";
  client.publish("topic", message.c_str());

  delay(5000); // Espera 5 segundos antes de enviar el siguiente mensaje
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando reconexión MQTT...");
    if (client.connect("arduinoClient")) { // Intenta reconectarse al servidor MQTT
      Serial.println("Conexión MQTT exitosa");
      //client.subscribe("topic"); // Suscríbete a un tema MQTT (opcional)
    } else {
      Serial.print("Error de conexión MQTT, esperando 5 segundos...");
      delay(5000);
    }
  }
}

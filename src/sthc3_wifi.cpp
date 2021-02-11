/* https://blog.zeruns.tech
 * Connection method
   * SHTC3 development board
   * SCL SCL (NodeMcu development board is D1)
   * SDA SDA (NodeMcu development board is D2)
   * https://randomnerdtutorials.com/esp8266-dht11dht22-temperature-and-humidity-web-server-with-arduino-ide/
 */
#include <Arduino.h>

#include <SparkFun_SHTC3.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <Authentication.h>

SHTC3 mySHTC3;

// Create AsyncWebServer object on port 80
AsyncWebServer http_server(80);

WiFiClient espClient;
PubSubClient mqtt_client(espClient);

// When was the last msg sent to mqtt
unsigned long mqtt_last_msg = 0;
// how often to send the message to mqtt
const long mqtt_send_interval = 120 * 1000;
// Credentials are declared, for easy maintenance, in Authentication.h
const char *room = "test-room";
const char *reader_name = "test";
// TODO: Template the 2 above items into the 2 below
const char *humidity_topic = "sensors/test-room/test/humidity";
const char *temperature_topic = "sensors/test-room/test/temperature";
char *temperature_metric_template = "temperature,room=%s,reader_name=%s temperature=%.2f";
char *humidity_metric_template = "humidity,room=%s,reader_name=%s humidity=%.2f";
// the buffer metric will hold the actual metric representations built from the templates above
char metric[128];

// Updated in loop
float RH, T;

const char *metrics = "# HELP room_temperature The temperature in Celsius of the room\n"
                      "# TYPE room_temperature gauge\n"
                      "room_temperature %TEMPERATURE%\n"
                      "# HELP room_relative_humidity The relative humidity of the room\n"
                      "# TYPE room_relative_humidity gauge\n"
                      "room_relative_humidity %HUMIDITY%\n";

// Replaces placeholder with DHT values
String processor(const String &var)
{
  //Serial.println(var);
  if (var == "TEMPERATURE")
  {
    return String(T);
  }
  else if (var == "HUMIDITY")
  {
    return String(RH);
  }
  return String();
}

// The errorDecoder function prints "SHTC3_Status_TypeDef" resultsin a human-friendly way
void errorDecoder(SHTC3_Status_TypeDef message)
{
  switch (message)
  {
  case SHTC3_Status_Nominal:
    Serial.print("Nominal");
    break;
  case SHTC3_Status_Error:
    Serial.print("Error");
    break;
  case SHTC3_Status_CRC_Fail:
    Serial.print("CRC Fail");
    break;
  default:
    Serial.print("Unknown return code");
    break;
  }
}

void setup_wifi()
{
  Serial.println("Connecting to WiFi");
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());
}

void mqtt_reconnect()
{
  // Loop until we're reconnected
  while (!mqtt_client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    if (mqtt_client.connect("nodeMcuTestSensor1", mqtt_username, mqtt_password))
    {
      Serial.println("connected");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup_http_server()
{
  http_server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(T).c_str());
  });
  http_server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(RH).c_str());
  });
  http_server.on("/metrics", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", metrics, processor);
  });
  // Start server
  http_server.begin();
}

void setup()
{                       //Initialization function, only run once at the beginning of the program
  Serial.begin(115200); //Set the serial port baud rate
  while (Serial == false)
  {
  };            //Wait for the serial connection to start
  Wire.begin(); //Initialize the Wire (IIC) library
  unsigned char i = 0;
  errorDecoder(mySHTC3.begin()); // To start the sensor you must call "begin()", the default settings use Wire (default Arduino I2C port)

  setup_wifi();
  setup_http_server();

  mqtt_client.setServer(mqtt_server, 1883);
}

void ensure_mqtt_client_connection()
{
  Serial.println("Connecting to MQTT");
  if (!mqtt_client.connected())
  {
    Serial.println("Reconnecting MQTT client");
    mqtt_reconnect();
  }
  mqtt_client.loop();
}

void publish_mqtt_metrics()
{
  // Wait a few seconds between measurements
  unsigned long now = millis();
  // last message is 0 only if default value, so no messages have been sent
  // or if, by a very slim chance, the rollover happend and it was set to 0
  if (now - mqtt_last_msg > mqtt_send_interval || mqtt_last_msg == 0)
  {
    mqtt_last_msg = now;

    Serial.println("Sending MQTT metrics!");
    ensure_mqtt_client_connection();

    // TODO: publish under 1 single topic?
    snprintf(metric, sizeof(metric), temperature_metric_template, room, reader_name, T);
    Serial.println(metric);
    mqtt_client.publish(temperature_topic, metric, false);

    snprintf(metric, sizeof(metric), humidity_metric_template, room, reader_name, RH);
    Serial.println(metric);
    mqtt_client.publish(humidity_topic, metric, false);
  }
  else
  {
    Serial.println("Not yet ready to send metrics!");
  }
}

void loop()
{
  SHTC3_Status_TypeDef result = mySHTC3.update();
  if (mySHTC3.lastStatus == SHTC3_Status_Nominal) //Determine whether the SHTC3 status is normal
  {
    RH = mySHTC3.toPercent(); //Read humidity data
    T = mySHTC3.toDegC();     //Read temperature data
  }
  else
  {
    Serial.print("Update failed, error: ");
    errorDecoder(mySHTC3.lastStatus); //Output error reason
    Serial.println();
  }

  Serial.print("Humidity:"); //Print Humidity to the serial port:
  Serial.print(RH);          //Print humidity data to the serial port
  Serial.print("%");
  Serial.print("  Temperature:");
  Serial.print(T); //Print temperature data to the serial port
  Serial.println("C");

  publish_mqtt_metrics();

  delay(5000);
}

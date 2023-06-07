#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <vector>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define SPEAKER_PIN D7
#define WIFI_SSID "Dialog 4G 544"
#define WIFI_PASS "5d1ddCFD" // Does not work for you
#define OFF_BTN D8

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Week Days
String weekDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// Month names
String months[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

int Speaker = SPEAKER_PIN; // GPIO13
WiFiServer server(80);

bool isRingingAlarm = false;

void displayTime()
{
  if (!isRingingAlarm)
  {
    display.stopscroll();
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);

    time_t epochTime = timeClient.getEpochTime();
    // Display date
    display.print(weekDays[timeClient.getDay()]);
    display.print(". ");

    struct tm *ptm = gmtime((time_t *)&epochTime);
    display.print(months[ptm->tm_mon]);
    display.println();
    display.println();

    // Display time
    display.setTextSize(2);
    display.print(timeClient.getHours() % 12);
    display.print(":");
    display.print(timeClient.getMinutes());
    display.display();
  }
  else
  {
    display.clearDisplay();
    display.display();
    display.setCursor(0, 0);
    display.setTextSize(3);
    display.print("ALARM");
    display.display();
  }
}

struct Alarm
{
  unsigned int hour;
  unsigned int minute;
};

std::vector<Alarm> alarms;

void sendAlarmsToServer(const String &json)
{
  WiFiClient client;
  HTTPClient http;

  if (http.begin(client, "http://home-s1.tronic247.com/"))
  {
    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
    client->setInsecure();

    http.addHeader("Content-Type", "application/json");
    // add valid headers such as user agent

    int httpCode = http.POST(json);
    if (httpCode == HTTP_CODE_OK)
    {
      Serial.println("Alarms saved successfully");
    }
    else
    {
      Serial.print("Failed to save alarms. Error code: ");
      Serial.println(httpCode);
    }

    http.end();
  }
  else
  {
    Serial.println("Failed to connect to server");
  }
}

String serializeAlarms()
{
  StaticJsonDocument<512> doc;

  JsonArray alarmsJson = doc.createNestedArray("alarms");

  for (const Alarm &alarm : alarms)
  {
    JsonObject alarmJson = alarmsJson.createNestedObject();
    alarmJson["hour"] = alarm.hour;
    alarmJson["minute"] = alarm.minute;
  }

  String json;

  serializeJson(doc, json);

  return json;
}

void saveAlarms()
{
  String json = serializeAlarms();
  sendAlarmsToServer(json);
}

void loadAlarms()
{
  WiFiClient client;
  HTTPClient http;

  if (http.begin(client, "http://home-s1.tronic247.com/"))
  {
    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
    client->setInsecure();

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
      String json = http.getString();

      StaticJsonDocument<512> doc;
      deserializeJson(doc, json);

      JsonArray alarmsJson = doc["alarms"];

      for (JsonObject alarmJson : alarmsJson)
      {
        Alarm alarm;
        alarm.hour = alarmJson["hour"];
        alarm.minute = alarmJson["minute"];
        alarms.push_back(alarm);
      }
    }
    else
    {
      Serial.print("Failed to load alarms. Error code: ");
      Serial.println(httpCode);
    }

    http.end();
  }
  else
  {
    Serial.println("Failed to connect to server");
  }
}

void addAlarm(unsigned int hour, unsigned int minute)
{
  Alarm alarm;
  alarm.hour = hour;
  alarm.minute = minute;
  alarms.push_back(alarm);
  saveAlarms();
}

void deleteAlarm(unsigned int hour, unsigned int minute)
{
  unsigned int alarmId = 0;

  for (const Alarm &alarm : alarms)
  {
    if (alarm.hour == hour && alarm.minute == minute)
    {
      alarms.erase(alarms.begin() + alarmId);
      break;
    }

    alarmId++;
  }

  saveAlarms();
}

unsigned int lastPlayedAlarmId = 0;

void checkAlarms()
{
  if (!isRingingAlarm)
  {
    for (const Alarm &alarm : alarms)
    {
      if (alarm.hour == timeClient.getHours() && alarm.minute == timeClient.getMinutes() && alarm.hour * 60 + alarm.minute != lastPlayedAlarmId)
      {
        isRingingAlarm = true;

        lastPlayedAlarmId = alarm.hour * 60 + alarm.minute;

        break; // Exit the loop after the first alarm is found
      }
    }
  }
}

const unsigned long toneDuration = 800;  // Duration of each tone in milliseconds
const unsigned long pauseDuration = 100; // Duration of pause between tones in milliseconds
unsigned long previousTime = 0;
int toneCounter = 0;

void alarm_music()
{
  unsigned long currentTime = millis();

  if (isRingingAlarm)
  {
    if (currentTime - previousTime >= toneDuration)
    {
      previousTime = currentTime;

      if (toneCounter % 2 == 0)
      {
        tone(SPEAKER_PIN, 800); // Play tone
      }
      else
      {
        noTone(SPEAKER_PIN); // Silence
      }

      toneCounter++;
    }
  }
  else
  {
  }
}

void setup()
{
  Serial.begin(9600);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }

  display.display();
  delay(2000);

  /**
   * Connect to the internet
   */
  WiFi.begin(ssid, password);
  Serial.println(WiFi.localIP());
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  /**
   * Connect to the NTP server
   */
  timeClient.begin();
  timeClient.setTimeOffset(19800);

  /**
   * Server start
   */
  server.begin();

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.display();

  /**
   * Pinmodes
   */
  pinMode(SPEAKER_PIN, OUTPUT);
  pinMode(OFF_BTN, INPUT);
  digitalWrite(SPEAKER_PIN, LOW);
  digitalWrite(OFF_BTN, LOW);

  /**
   * Alarm stuff
   */
  loadAlarms();
}

unsigned long lastUpdate = 30000;

int buttonState = 0;

void loop()
{
  // Update the time every 30 seconds
  if (millis() - lastUpdate > 30000)
  {
    timeClient.update();
    lastUpdate = millis();
  }

  /*
  Todo: figure out a more efficient way to do this
  */
  checkAlarms();
  alarm_music();
  displayTime();

  buttonState = digitalRead(OFF_BTN);

  if (buttonState == HIGH)
  {
    isRingingAlarm = false;
    noTone(SPEAKER_PIN);
  }

  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client)
  {
    return;
  }

  while (!client.available())
  {
    delay(1);
  }

  // Read the first line of the request
  String request = client.readStringUntil('\r');
  client.flush();

  if (request.indexOf("/ALARM") != -1)
  {
    isRingingAlarm = true;
  }

  /**
   * If request has /ADDALARM/HH/MM
   */
  if (request.indexOf("/ADDALARM/") != -1)
  {
    int hourIndex = request.indexOf("/ADDALARM/") + 10;
    int hour = request.substring(hourIndex, hourIndex + 2).toInt();

    int minuteIndex = hourIndex + 3;
    int minute = request.substring(minuteIndex, minuteIndex + 2).toInt();

    addAlarm(hour, minute);
  }

  /**
   * If request has /DELETEALARM/HH/MM
   */
  if (request.indexOf("/DELETEALARM/") != -1)
  {
    int hourIndex = request.indexOf("/DELETEALARM/") + 13;
    int hour = request.substring(hourIndex, hourIndex + 2).toInt();

    int minuteIndex = hourIndex + 3;
    int minute = request.substring(minuteIndex, minuteIndex + 2).toInt();

    deleteAlarm(hour, minute);
  }

  /**
   * If request is ALARMS
   */
  if (request.indexOf("/JSON") != -1)
  {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println(""); //  do not forget this one

    client.println(serializeAlarms());
  }
  else if (request.indexOf("/STOP") != -1)
  {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println(""); //  do not forget this one
    client.println("1");
    ESP.restart();
  }
  else
  {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println(""); //  do not forget this one
    client.println(R"html(
    <!DOCTYPE html>
<html>
	<head>
		<link
			href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css"
			rel="stylesheet"
			integrity="sha384-9ndCyUaIbzAi2FUVXJi0CjmCapSmO7SnpJef0486qhLnuZ2cdeRhO02iuK6FUUVM"
			crossorigin="anonymous"
		/>
	</head>

	<div class="p-4 container-md m-auto">
		<h1>Alarms</h1>
		<button href="/ALARM" class="btn btn-primary" id="trigger">Trigger</button>
    <button href="/STOP" class="btn btn-danger ms-2" id="stop">Stop</button>

		<br /><br />

		<div class="row g-3">
			<div class="col-auto">
				<label for="staticEmail2" class="visually-hidden">HH</label>
				<input type="number" class="form-control" id="HH" />
			</div>
			<div class="col-auto">
				<label for="inputPassword2" class="visually-hidden">MM</label>
				<input type="number" class="form-control" id="MM" />
			</div>
			<div class="col-auto">
				<button type="submit" class="btn btn-primary mb-3" onclick="addAlarm()">
					Add Alarm
				</button>
			</div>
		</div>

		<h2>Current Alarms</h2>
		<div id="alarms"></div>
	</div>

	<link
		href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css"
		rel="stylesheet"
		integrity="sha384-9ndCyUaIbzAi2FUVXJi0CjmCapSmO7SnpJef0486qhLnuZ2cdeRhO02iuK6FUUVM"
		crossorigin="anonymous"
	/>
	<script
		src="https://code.jquery.com/jquery-3.7.0.min.js"
		integrity="sha256-2Pmvv0kuTBOenSvLm6bvfBSSHrUJ+3A7x6P5Ebd07/g="
		crossorigin="anonymous"
	></script>

	<script>
		$("#trigger").click(function () {
			let t = $(this);
			$(this).attr("disabled", true);
			$.ajax({
				url: "/ALARM",
				//when the request is done
				complete: function () {
					setTimeout(function () {
						t.attr("disabled", false);
					}, 8000);
				},
			});
		});

    $("#stop").click(function () {
			let t = $(this);
			$(this).attr("disabled", true);

			$.ajax({
				url: "/STOP",
			});

      let i = 1;
					const interval = setInterval(function () {
						t.text(`Reconnecting (${i++}s)`);
						$.ajax({
							url: "/",
							complete: function (data) {
								clearInterval(interval);
								t.attr("disabled", false);
                t.text("Stop");
							},
						});
					}, 1000);
		});

		function addAlarm() {
			let HH = +$("#HH").val();
			let MM = +$("#MM").val();

			//add 0 if HH or MM is less than 10
			if (HH < 10) {
				HH = "0" + HH;
			}
			if (MM < 10) {
				MM = "0" + MM;
			}

			$.ajax({
				url: `/ADDALARM/${HH}/${MM}`,
				complete: function () {
					window.location.reload();
				},
			});
		}
    const alarms = [
)html");
    for (const Alarm &alarm : alarms)
    {
      client.print("[");
      client.print(alarm.hour);
      client.print(",");
      client.print(alarm.minute);
      client.print("],");
    }

    client.println(R"html(
  ];
		alarms.forEach((alarm) => {
			$("#alarms").append(
				`<div class="row g-3">
					<div class="col-auto">
						<label for="staticEmail2" class="visually-hidden">HH</label>
						<input type="number" readonly class="form-control" id="staticEmail2" value="${alarm[0]}" />
					</div>
					<div class="col-auto">
						<label for="inputPassword2" class="visually-hidden">MM</label>
						<input type="number" readonly class="form-control" id="inputPassword2" value="${alarm[1]}" />
					</div>
					<div class="col-auto">
						<button type="submit" class="btn btn-danger mb-3" onclick="deleteAlarm(${alarm[0]}, ${alarm[1]})">
							Delete
						</button>
					</div>
				</div>`
			);
		});

		function deleteAlarm(HH, MM) {
      //add 0 if HH or MM is less than 10
			if (HH < 10) {
				HH = "0" + HH;
			}
			if (MM < 10) {
				MM = "0" + MM;
			}

			$.ajax({
				url: `/DELETEALARM/${HH}/${MM}`,
				complete: function () {
					window.location.reload();
				},
			});
		}
	</script>
</html>
    )html");
  }
}

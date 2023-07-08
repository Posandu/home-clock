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
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define SPEAKER_PIN D5
#define WIFI_SSID "Dialog 4G 544"
#define WIFI_PASS "5d1ddCFD"
#define OFF_BTN D6

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

void renderViews()
{
    if (!isRingingAlarm)
    {
        display.stopscroll();
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(0, 0);

        time_t epochTime = timeClient.getEpochTime();
        display.print(weekDays[timeClient.getDay()]);
        display.print(", ");
        struct tm *ptm = gmtime((time_t *)&epochTime);
        display.print(months[ptm->tm_mon]);
        display.print(" ");
        display.print(timeClient.getDay());
        display.println();
        display.println();

        // Display time
        display.setTextSize(4);
        display.print(timeClient.getHours() % 12 == 0 ? 12 : timeClient.getHours() % 12);
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
    unsigned int weekend;
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
        alarmJson["weekend"] = alarm.weekend;
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
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK)
        {
            String json = http.getString();

            StaticJsonDocument<512> doc;
            deserializeJson(doc, json);

            JsonArray alarmsJson = doc["alarms"];

            Serial.println("Alarms loaded successfully");
            Serial.println(json);

            for (JsonObject alarmJson : alarmsJson)
            {
                Alarm alarm;
                alarm.hour = alarmJson["hour"];
                alarm.minute = alarmJson["minute"];
                alarm.weekend = alarmJson["weekend"];
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

void addAlarm(unsigned int hour, unsigned int minute, unsigned int weekend = 0)
{
    Alarm alarm;
    alarm.hour = hour;
    alarm.minute = minute;
    alarm.weekend = weekend;
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
            int alarmTime = alarm.hour * 60 + alarm.minute;
            int currentDay = timeClient.getDay();
            bool isWeekend = (currentDay == 0 || currentDay == 6);

            if (alarm.hour == timeClient.getHours() && alarm.minute == timeClient.getMinutes() && alarmTime != lastPlayedAlarmId &&
                ((alarm.weekend == 1 && isWeekend)))
            {
                isRingingAlarm = true;
                lastPlayedAlarmId = alarmTime;
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
                tone(SPEAKER_PIN, 600); // Play tone
            }
            else
            {
                noTone(SPEAKER_PIN); // Silence
            }

            toneCounter++;
        }
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
    delay(1000);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    // display.setRotation(2);
    display.println("Loading...");
    display.display();
    delay(2000);

    /**
     * Connect to the internet
     */
    WiFi.begin(ssid, password);
    /*Keep trying to connect to the internet until it is connected*/
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println(WiFi.localIP());

    /**
     * Connect to the NTP server
     */
    timeClient.begin();
    timeClient.setTimeOffset(19800);
    timeClient.forceUpdate();

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

    /**
     * Alarm stuff
     */
    loadAlarms();
}

void loop()
{
    /*
    Todo: figure out a more efficient way to do this
    */
    alarm_music();
    checkAlarms();
    renderViews();

    if (digitalRead(OFF_BTN) == HIGH)
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
        noTone(SPEAKER_PIN); // Silence
        isRingingAlarm = true;
    }

    /**
     * If request has /ADDALARM/HH/MM/W
     */
    if (request.indexOf("/ADDALARM/") != -1)
    {
        int hourIndex = request.indexOf("/ADDALARM/") + 10;
        int hour = request.substring(hourIndex, hourIndex + 2).toInt();

        int minuteIndex = hourIndex + 3;
        int minute = request.substring(minuteIndex, minuteIndex + 2).toInt();

        int weekendIndex = minuteIndex + 3;
        int weekend = request.substring(weekendIndex, weekendIndex + 1).toInt();

        addAlarm(hour, minute, weekend);
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
        client.println("{\"running_for\": " + String(millis() / 1000) + ", \"alarms\": ");
        client.println(serializeAlarms());
        client.println("}");
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
        client.println(""); // do not forget this one
        client.println(R"html(
<!DOCTYPE html>
<html>
	<head>
		  <script src="https://cdn.tailwindcss.com"></script>
	</head>

	<div class="m-auto max-w-2xl shadow-xl border rounded-lg px-6 py-4 mt-8">
		<button class="bg-blue-500 border border-transparent text-white py-2 px-4 rounded transition-all hover:bg-blue-700 active:bg-blue-800" id="trigger">Manually Trigger the alarm</button>
		<button class="border border-red-500 text-red-600 py-2 px-4 rounded transition-all hover:bg-red-100 active:bg-red-200" id="stop">Restart</button>
        <button class="border border-transparent bg-gray-500 text-white py-2 px-4 rounded transition-all hover:bg-gray-600 active:bg-gray-700" id="loadLogs">Load Logs</button>

		<br /><br />

		<h1 class="text-2xl font-semibold mb-2">Add Alarm</h1>

		<div class="flex items-center space-x-2">
			<div>
				<label for="HH" class="sr-only">HH</label>
				<input type="number" class="border focus:ring border-gray-300 rounded-md outline-none px-4 py-2" id="HH" />
			</div>
			<div>
				<label for="MM" class="sr-only">MM</label>
				<input type="number" class="border focus:ring border-gray-300 rounded-md outline-none px-4 py-2" id="MM" />
			</div>
      <div>
				<label for="MM" class="sr-only">Weekends?</label>
				<input type="checkbox" class="border focus:ring border-gray-300 rounded-md outline-none px-4 py-2" id="ONWEEKENDS" />
			</div>
			<div>
				<button type="submit" class="bg-blue-500 border border-transparent text-white py-2 px-4 rounded transition-all hover:bg-blue-700 active:bg-blue-800" onclick="addAlarm()">
					Add Alarm
				</button>
			</div>
		</div>

			<h2 class="text-2xl font-semibold my-2">Current Alarms</h2>
			<div id="alarms"></div>

            <h2 class="text-2xl font-semibold my-2">Log</h2>
            <pre id="LOG"></pre>
		</div>
	</div>

	<script src="https://code.jquery.com/jquery-3.7.0.min.js" integrity="sha256-2Pmvv0kuTBOenSvLm6bvfBSSHrUJ+3A7x6P5Ebd07/g=" crossorigin="anonymous"></script>

	<script>
        function addLog(...a){
            $("#LOG").append(a.join(" ") + "\n");
        }

        addLog("Started");

		$("#trigger").click(function () {
			let t = $(this);
			$(this).attr("disabled", true);

			$.ajax({
				url: "/ALARM",
				//when the request is done
				complete: function () {
					setTimeout(function () {
						t.attr("disabled", false);
					}, 800);

                    addLog("Triggered");
				},
			});
		});

		$("#stop").click(function () {
			let t = $(this);
			$(this).attr("disabled", true);

			$.ajax({
				url: "/STOP",
			});

            addLog("Stopped");

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
				url: `/ADDALARM/${HH}/${MM}/${$("#ONWEEKENDS").is(":checked") ? 1 : 0}`,
				complete: function () {
					window.location.reload();
				},
			});
		}

        $("#loadLogs").click(function(){
            $.ajax({
                url: "/JSON",
                complete: function(data){
                    addLog("Loaded Logs");

                    const blob = new Blob([data.responseText], {type: "text/json"});
                    window.open(URL.createObjectURL(blob), "Logs", "width=600,height=400,scrollbars=yes");
                }
            });
        });

		const alarms = [
)html");
        for (const Alarm &alarm : alarms)
        {
            client.print("[");
            client.print(alarm.hour);
            client.print(",");
            client.print(alarm.minute);
            client.print(",");
            client.print(alarm.weekend);
            client.print("],");
        }

        client.println(R"html(
		];
		alarms.forEach((alarm) => {
			$("#alarms").append(
				`<div class="flex items-center space-x-2 mb-3">
					<div>
						<label for="staticEmail2" class="sr-only">HH</label>
						<input type="number" readonly class="border border-gray-300 rounded-md px-4 py-2" id="staticEmail2" value="${alarm[0]}" />
					</div>
					<div>
						<label for="inputPassword2" class="sr-only">MM</label>
						<input type="number" readonly class="border border-gray-300 rounded-md px-4 py-2" id="inputPassword2" value="${alarm[1]}" />
					</div>
					<div>
           ${alarm[2] == 1 ? '<span class="text-green-500">Also on weekends</span>' : ""}
						<button type="submit" class="bg-red-500 border border-transparent text-white py-2 px-4 rounded transition-all hover:bg-red-700 active:bg-red-800" onclick="deleteAlarm(${alarm[0]}, ${alarm[1]})">
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
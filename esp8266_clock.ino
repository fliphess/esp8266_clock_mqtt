#include <FS.h>
#include <EEPROM.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <time.h>
#include <simpleDSTadjust.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

#include "settings.h"

// * Include the PubSubClient after setting mqtt packet size in settings.h
#include <PubSubClient.h>

// * Initiate led blinker library
Ticker ticker;

// * Initiate the seconds ticker that updates our led display
int32_t tick;

// * Setup simpleDSTadjust Library rules
simpleDSTadjust dstAdjusted(StartRule, EndRule);

// * Initiate HTTP server
Adafruit_7segment matrix = Adafruit_7segment();

// * Initiate WIFI client
WiFiClient espClient;

// * Initiate MQTT client
PubSubClient mqtt_client(espClient);


// **********************************
// * Time & NTP                     *
// **********************************

// * Flag changed in the ticker function to start NTP Update
bool ntp_ready_for_update = false;

// * NTP timer update ticker
void seconds_ticker() {
    tick--;

    if (tick <= 0) {
        tick = NTP_UPDATE_INTERVAL_SEC; // Re-arm
        ntp_ready_for_update = true;
    }

    if (!ntp_ready_for_update) {
        update_clock_display();
    }

}   // * end seconds_ticker()


void get_time_from_ntp_servers() {
    bool update_time_ok = false;
    int cnt = 10;

    Serial.print(F("\nTrying to update time from NTP Server "));
    configTime(timezone * 3600, 0, ntp_server1, ntp_server2, ntp_server3);
    delay(500);

    while (!update_time_ok && (cnt > 0)) {
        update_time_ok = time(nullptr);
        if (!update_time_ok) {
            Serial.print("#");
            delay(1000);
            cnt--;
        }
    }

    if (update_time_ok) {
        Serial.print(F("\nUpdated time from NTP Server: "));
        print_time(0);
    }
    else {
        Serial.print(F("\nFailed to update time from NTP Server"));
        tick = 60; // Re-arm to test again within a minute.
    }

    Serial.print(F("\nNext NTP Update: "));
    print_time(tick);

}   // * end get_time_from_ntp_servers()


void print_time(time_t offset) {
    char buf[30];
    char *dstAbbrev;
    time_t t = dstAdjusted.time(&dstAbbrev)+offset;
    struct tm *timeinfo = localtime (&t);

    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d %s\n", timeinfo->tm_year+1900, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, dstAbbrev);
    Serial.print(buf);

}   // * end print_time()


void update_clock_display() {
    char buf[30];
    char *dstAbbrev;
    time_t t = dstAdjusted.time(&dstAbbrev);
    struct tm *timeinfo = localtime (&t);

    float tmpTime = (timeinfo->tm_hour * 100) + timeinfo->tm_min;

    matrix.print(tmpTime);
    matrix.drawColon(true);
    matrix.writeDisplay();

}   // * end update_clock_display()


// **********************************
// * WIFI                           *
// **********************************

// * Gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager) {
    Serial.println(F("Entered config mode"));
    Serial.println(WiFi.softAPIP());

    // * If you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());

    // * Entered config mode, make led toggle faster
    ticker.attach(0.2, led_tick);

}   // * end configModeCallback()


// **********************************
// * Ticker (System LED Blinker)    *
// **********************************

// * Blink on-board Led
void led_tick() {
    // * Toggle state
    int state = digitalRead(BUILTIN_LED);    // * Get the current state of GPIO1 pin
    digitalWrite(BUILTIN_LED, !state);       // * Set pin to the opposite state

}   // * end led_tick()


// **********************************
// * MQTT                           *
// **********************************

// * Callback for incoming MQTT messages
void mqtt_callback(char* topic, byte* payload_in, unsigned int length) {
    // Empty until i need the clock to do something (like reset)

}   // * end mqtt_callback()

// * Reconnect to MQTT server and subscribe to in and out topics
bool mqtt_reconnect() {
    // * Loop until we're reconnected
    int MQTT_RECONNECT_RETRIES = 0;

    while (!mqtt_client.connected() && MQTT_RECONNECT_RETRIES < MQTT_MAX_RECONNECT_TRIES) {
        MQTT_RECONNECT_RETRIES++;

        Serial.printf("MQTT connection attempt %d / %d ...\n", MQTT_RECONNECT_RETRIES, MQTT_MAX_RECONNECT_TRIES);

        // * Attempt to connect
        if (mqtt_client.connect(HOSTNAME, MQTT_USER, MQTT_PASS)) {

            Serial.println(F("MQTT connected!"));

            // * Once connected, publish an announcement...
            char * message = new char[23 + strlen(HOSTNAME) + 1];
            strcpy(message, "alarmclock alive: ");
            strcat(message, HOSTNAME);
            mqtt_client.publish("hass/status", message);

            // * Resubscribe to the incoming mqtt topic
            mqtt_client.subscribe(MQTT_IN_TOPIC);

            Serial.printf("MQTT topic in: %s\n", MQTT_IN_TOPIC);
            Serial.printf("MQTT topic out: %s\n", MQTT_OUT_TOPIC);

        }
        else {
            Serial.print(F("MQTT Connection failed: rc="));
            Serial.println(mqtt_client.state());
            Serial.println(F(" Retrying in 5 seconds"));
            Serial.println("");

            // * Wait 5 seconds before retrying
            delay(5000);
        }
    }

    if (MQTT_RECONNECT_RETRIES >= MQTT_MAX_RECONNECT_TRIES) {
        Serial.printf("*** MQTT connection failed, giving up after %d tries ...\n", MQTT_RECONNECT_RETRIES);
        return false;
    }

    return true;

}   // * end mqtt_reconnect()


int button_state;                        // the current reading from the input pin
int last_button_state = MQTT_BUTTON_OFF;  // the previous reading from the input pin

unsigned long last_bounce_time = 0;     // the last time the output pin was toggled
unsigned long debounce_delay    = 50;    // the debounce time; increase if the output flickers


void send_update_to_broker() {
    const char* output = (button_state == MQTT_BUTTON_ON) ? MQTT_ON_MESSAGE : MQTT_OFF_MESSAGE;

    Serial.print("Button state changed to ");
    Serial.println(output);

    if (!mqtt_client.publish(MQTT_OUT_TOPIC, output)) {
        Serial.println(F("Failed to publish state to broker"));
    }
}


// * Check if the button state changed and send to MQTT broker
void button_loop() {
    int reading = digitalRead(BUTTON_PIN);

    if (reading != last_button_state) {
        last_bounce_time = millis();
    }

    if ((millis() - last_bounce_time) > debounce_delay) {
        if (reading != button_state) {
            button_state = reading;
            send_update_to_broker();
        }
    }
    last_button_state = reading;

} // end button_loop()


// **********************************
// * EEPROM helpers                 *
// **********************************

String read_eeprom(int offset, int len) {
    String res = "";
    for (int i = 0; i < len; ++i) {
        res += char(EEPROM.read(i + offset));
    }
    Serial.print(F("read_eeprom(): "));
    Serial.println(res.c_str());

    return res;

}   // * end read_eeprom()

void write_eeprom(int offset, int len, String value) {

    Serial.print(F("write_eeprom(): "));
    Serial.println(value.c_str());

    for (int i = 0; i < len; ++i) {
        if ((unsigned)i < value.length()) {
            EEPROM.write(i + offset, value[i]);
        }
        else {
            EEPROM.write(i + offset, 0);
        }
    }

}   // * end write_eeprom()


// ******************************************
// * Callback for saving WIFI config        *
// ******************************************

bool shouldSaveConfig = false;

// * Callback notifying us of the need to save config
void save_wifi_config_callback () {
    Serial.println(F("Should save config"));
    shouldSaveConfig = true;

}   // * end save_wifi_config_callback()


// **********************************
// * Setup OTA                      *
// **********************************

void setup_ota() {
    Serial.println(F("Arduino OTA activated."));

    // * Port defaults to 8266
    ArduinoOTA.setPort(8266);

    // * Set hostname for OTA
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        Serial.println(F("Arduino OTA: Start"));
    });

    ArduinoOTA.onEnd([]() {
        Serial.println(F("Arduino OTA: End (Running reboot)"));
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Arduino OTA Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Arduino OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println(F("Arduino OTA: Auth Failed"));
        else if (error == OTA_BEGIN_ERROR)
            Serial.println(F("Arduino OTA: Begin Failed"));
        else if (error == OTA_CONNECT_ERROR)
            Serial.println(F("Arduino OTA: Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println(F("Arduino OTA: Receive Failed"));
        else if (error == OTA_END_ERROR)
            Serial.println(F("Arduino OTA: End Failed"));
    });

    ArduinoOTA.begin();

    Serial.println(F("Arduino OTA finished"));

}   // * end setup_ota()


// **********************************
// * Setup MDNS discovery service   *
// **********************************

void setup_mdns() {
    Serial.println(F("Starting MDNS responder service"));
    bool mdns_result = MDNS.begin(HOSTNAME);

}   // * end setup_mdns()


// **********************************
// * Setup Main                     *
// **********************************

void setup(){
    // * Configure Serial and EEPROM
    Serial.begin(115200);
    EEPROM.begin(512);
    matrix.begin(0x70);

    // * Set led pin as output
    pinMode(BUILTIN_LED, OUTPUT);

    // * Set button pin to input pulldown
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // * Start ticker with 0.5 because we start in AP mode and try to connect
    ticker.attach(0.6, led_tick);

    // * Get MQTT Server settings
    String settings_available = read_eeprom(134, 1);

    if (settings_available == "1") {
        read_eeprom(0, 64).toCharArray(MQTT_HOST, 64);   // * 0-63
        read_eeprom(64, 6).toCharArray(MQTT_PORT, 6);    // * 64-69
        read_eeprom(70, 32).toCharArray(MQTT_USER, 32);  // * 70-101
        read_eeprom(102, 32).toCharArray(MQTT_PASS, 32); // * 102-133
    }

    WiFiManagerParameter CUSTOM_MQTT_HOST("host", "MQTT hostname", MQTT_HOST, 64);
    WiFiManagerParameter CUSTOM_MQTT_PORT("port", "MQTT port",     MQTT_PORT, 6);
    WiFiManagerParameter CUSTOM_MQTT_USER("user", "MQTT user",     MQTT_USER, 32);
    WiFiManagerParameter CUSTOM_MQTT_PASS("pass", "MQTT pass",     MQTT_PASS, 32);

    // * WiFiManager local initialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // * Reset settings - uncomment for testing
    // wifiManager.resetSettings();

    // * Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    // * Set timeout
    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);

    // * Set save config callback
    wifiManager.setSaveConfigCallback(save_wifi_config_callback);

    // * Add all your parameters here
    wifiManager.addParameter(&CUSTOM_MQTT_HOST);
    wifiManager.addParameter(&CUSTOM_MQTT_PORT);
    wifiManager.addParameter(&CUSTOM_MQTT_USER);
    wifiManager.addParameter(&CUSTOM_MQTT_PASS);

    // * Fetches SSID and pass and tries to connect
    // * Reset when no connection after 10 seconds
    if (!wifiManager.autoConnect()) {
        Serial.println(F("Failed to connect to WIFI and hit timeout"));

        // * Reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(WIFI_TIMEOUT);
    }

    // * Read updated parameters
    strcpy(MQTT_HOST, CUSTOM_MQTT_HOST.getValue());
    strcpy(MQTT_PORT, CUSTOM_MQTT_PORT.getValue());
    strcpy(MQTT_USER, CUSTOM_MQTT_USER.getValue());
    strcpy(MQTT_PASS, CUSTOM_MQTT_PASS.getValue());

    // * Save the custom parameters to FS
    if (shouldSaveConfig) {
        Serial.println(F("Saving WiFiManager config"));
        write_eeprom(0, 64, MQTT_HOST);   // * 0-63
        write_eeprom(64, 6, MQTT_PORT);   // * 64-69
        write_eeprom(70, 32, MQTT_USER);  // * 70-101
        write_eeprom(102, 32, MQTT_PASS); // * 102-133
        write_eeprom(134, 1, "1");        // * 134 --> always "1"
        EEPROM.commit();
    }

    // * If you get here you have connected to the WiFi
    Serial.println(F("Connected to WIFI..."));

    // * Keep LED on
    digitalWrite(BUILTIN_LED, LOW);

    // * Initialize the NTP update countdown ticker
    tick = NTP_UPDATE_INTERVAL_SEC;

    // Get the current time from the time server
    get_time_from_ntp_servers();

    // Set a ticker that runs every second
    ticker.detach();
    ticker.attach(1, seconds_ticker); // Run a 1 second interval Ticker

    // * Configure OTA
    setup_ota();

    // * Startup MDNS Service
    setup_mdns();

    // * Configure MQTT
    snprintf(MQTT_IN_TOPIC, sizeof MQTT_IN_TOPIC, "%s/in", HOSTNAME);
    snprintf(MQTT_OUT_TOPIC, sizeof MQTT_OUT_TOPIC, "%s/out", HOSTNAME);

    mqtt_client.setServer(MQTT_HOST, atoi(MQTT_PORT));
    mqtt_client.setCallback(mqtt_callback);

    Serial.printf("MQTT active: %s:%s\n", MQTT_HOST, MQTT_PORT);

}   // * end setup()


// **********************************
// * Loop                           *
// **********************************

void loop() {
    ArduinoOTA.handle();

    // * Update NTP when needed
    if (ntp_ready_for_update) {
        get_time_from_ntp_servers();
        ntp_ready_for_update = false;
    }

    // * Maintain MQTT connection
    if (!mqtt_client.connected()) {
        long now = millis();

        if (now - LAST_RECONNECT_ATTEMPT > 5000) {
            LAST_RECONNECT_ATTEMPT = now;

            if (mqtt_reconnect()) {
                LAST_RECONNECT_ATTEMPT = 0;
            }
        }
    }
    else {
        mqtt_client.loop();
    }

    button_loop();

}   // * end loop()

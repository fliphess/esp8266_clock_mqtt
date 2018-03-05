
// * Set the mqtt button pin
#define BUTTON_PIN D0

// * MQTT network settings
#define MQTT_MAX_PACKET_SIZE 512

// * After 10 retry connects, cancel routine and return to loop
#define MQTT_MAX_RECONNECT_TRIES 10

// * Button pressed, esp8266 returns low
#define MQTT_BUTTON_ON LOW

// * Button not pressed returns HIGH
#define MQTT_BUTTON_OFF HIGH

// * The message to send when ON
#define MQTT_ON_MESSAGE "{\"state\": \"ON\"}"

// * The message to send when OFF
#define MQTT_OFF_MESSAGE "{\"state\": \"OFF\"}"

// * The hostname of the clock
#define HOSTNAME "alarmclock1.home"

// * The password used for uploading
#define OTA_PASSWORD "admin"

// * Webserver http port to listen on
#define HTTP_PORT 80

// * Wifi timeout in milliseconds
#define WIFI_TIMEOUT 30000

// * To be filled with EEPROM data
char MQTT_HOST[64] = "";
char MQTT_PORT[6]  = "";
char MQTT_USER[32] = "";
char MQTT_PASS[32] = "";

// * MQTT in and out topic based on hostname
char MQTT_IN_TOPIC[strlen(HOSTNAME) + 4];      // * Topic in will be: <HOSTNAME>/in
char MQTT_OUT_TOPIC[strlen(HOSTNAME) + 5];     // * Topic out will be: <HOSTNAME>/out

// * Define the MQTT output buffer
char MQTT_OUTPUT_BUFFER[50];

// * MQTT Last reconnection counter
long LAST_RECONNECT_ATTEMPT = 0;

char ntp_server1[40] = "0.nl.pool.ntp.org";
char ntp_server2[40] = "1.nl.pool.ntp.org";
char ntp_server3[40] = "2.nl.pool.ntp.org";

// -------------- Configuration options -----------------

// * Update time from NTP server every 2 hours
#define NTP_UPDATE_INTERVAL_SEC 2 * 3600

// * Central European Time zone
#define timezone +1 // Central European Time zone

struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 3600};       // Daylight time = UTC/GMT +2 hours
struct dstRule EndRule   = {"CET",  Last, Sun, Oct, 2, 0};          // Standard time = UTC/GMT +1 hour



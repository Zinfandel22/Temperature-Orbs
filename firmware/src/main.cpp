#include <Arduino.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef HUB_BLYNK_SERVER
#define HUB_BLYNK_SERVER "blynk.cloud"
#endif
#if defined(BLYNK_AUTH_TOKEN)
#error "Remove BLYNK_AUTH_TOKEN from secrets.h; Blynk app provisioning supplies it."
#endif

#define BLYNK_FIRMWARE_VERSION "0.1.0"
#define BLYNK_PRINT Serial
#include <BlynkEdgent.h>
#include "BoardConfig.h"
// GPIO 2 is reserved as BUSY_PIN on this board, not an Edgent status LED.
#if defined(BOARD_LED_PIN) && BOARD_LED_PIN == BUSY_PIN
#undef BOARD_LED_PIN
#endif
#include "Interaction.h"
#include "HubData.h"
#include "OrbData.h"
#include <ArduinoHttpClient.h>
#include <ESP_SSLClient.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>

namespace {
constexpr size_t displayCount = 5;
constexpr size_t drawBufferLines = 5;
constexpr uint32_t syncIntervalMs = 10000;
int displayPins[displayCount] = {
    SCREEN_1_CS, SCREEN_2_CS, SCREEN_3_CS, SCREEN_4_CS, SCREEN_5_CS
};

TFT_eSPI tft;
lv_color_t drawPixels[SCREEN_SIZE * drawBufferLines];
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDrivers[displayCount];

struct OrbDisplay {
    lv_obj_t* screen;
    lv_obj_t* temperature;
    lv_obj_t* humidity;
    lv_obj_t* battery;
    lv_obj_t* contact;
    lv_obj_t* status;
    int scaleDegrees;
};

OrbDisplay orbs[displayCount];
bool edgentStarted = false;
bool wasBlynkConnected = false;
bool timeConfigured = false;
bool timeWarningLogged = false;
bool hubUpdatePending = false;
HubPollStatus hubPollStatus;
uint32_t timeConfigStarted = 0;
int64_t hubUpdateReceivedUs = 0;

struct HubPollResult {
    HubReadings readings;
    uint32_t tokenGeneration;
    bool success;
};

QueueHandle_t hubResults = nullptr;
TaskHandle_t hubPollTask = nullptr;
portMUX_TYPE hubTokenMux = portMUX_INITIALIZER_UNLOCKED;
char hubToken[33] = {};
uint32_t hubTokenGeneration = 0;

bool validHubServer() {
    const char* server = HUB_BLYNK_SERVER;
    const size_t serverLength = strlen(server);
    const char suffix[] = ".blynk.cloud";
    if (strcmp(server, "blynk.cloud") != 0 &&
        (serverLength <= sizeof(suffix) - 1 ||
         strcmp(server + serverLength - (sizeof(suffix) - 1), suffix) != 0)) return false;
    for (const char* c = server; *c; ++c) {
        if (!isalnum(static_cast<unsigned char>(*c)) && *c != '-' && *c != '.') return false;
    }
    return true;
}

bool fetchHubReadings(HubReadings& readings, const char* token) {
    WiFiClient transport;
    bssl::X509List trustAnchors;
    if (!trustAnchors.append(ROOT_CA_CERT) || trustAnchors.getCount() == 0) {
        Serial.println("Failed to load Hub HTTPS trust anchors");
        return false;
    }
    ESP_SSLClient network;
    network.setClient(&transport);
    network.setTrustAnchors(&trustAnchors);
    network.setBufferSizes(16384, 512);
    network.setHandshakeTimeout(10);
    network.setTimeout(5);
    if (!network.setSSLVersion(BR_TLS12, BR_TLS12)) {
        Serial.println("Failed to configure Hub HTTPS TLS version");
        return false;
    }
    HttpClient http(network, HUB_BLYNK_SERVER, 443);
    http.setHttpResponseTimeout(5000);
    http.setTimeout(5000);

    String path = "/external/api/getAll?token=";
    path += token;
    constexpr uint32_t heapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t freeBefore = heap_caps_get_free_size(heapCaps);
    const size_t largestBefore = heap_caps_get_largest_free_block(heapCaps);
    const int requestResult = http.get(path);
    if (requestResult != 0) {
        Serial.printf("Hub HTTPS connection failed (%d)\n", requestResult);
        Serial.printf("Internal heap before HTTPS: %u bytes free, %u largest block; after: %u free, %u largest block\n",
                      static_cast<unsigned>(freeBefore), static_cast<unsigned>(largestBefore),
                      static_cast<unsigned>(heap_caps_get_free_size(heapCaps)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(heapCaps)));
        return false;
    }
    const int status = http.responseStatusCode();
    if (status != 200) {
        Serial.printf("Hub HTTPS request failed (status %d); check token and server region\n", status);
        http.stop();
        return false;
    }
    const int headers = http.skipResponseHeaders();
    if (headers != 0) {
        Serial.printf("Hub HTTPS response headers failed (%d)\n", headers);
        http.stop();
        return false;
    }

    JsonDocument filter;
    for (uint8_t pin : hubDataPins) {
        char key[5];
        snprintf(key, sizeof(key), "v%u", static_cast<unsigned>(pin));
        filter[key] = true;
    }
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, http, DeserializationOption::Filter(filter));
    http.stop();
    if (error || !document.is<JsonObjectConst>()) {
        Serial.printf("Invalid Hub HTTPS response: %s\n", error ? error.c_str() : "expected JSON object");
        return false;
    }
    if (!decodeHubReadings(document.as<JsonObjectConst>(), readings)) {
        Serial.println("Hub HTTPS response contains no usable datastream values");
        return false;
    }
    return true;
}

void pollHubTask(void*) {
    for (;;) {
        char token[sizeof(hubToken)];
        HubPollResult result = {};
        portENTER_CRITICAL(&hubTokenMux);
        memcpy(token, hubToken, sizeof(token));
        result.tokenGeneration = hubTokenGeneration;
        portEXIT_CRITICAL(&hubTokenMux);
        if (!token[0]) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Hub data unavailable: Wi-Fi disconnected");
        } else if (time(nullptr) < 1577836800) {
            Serial.println("Hub data unavailable: waiting for clock synchronization");
        } else {
            result.success = fetchHubReadings(result.readings, token);
        }
        xQueueOverwrite(hubResults, &result);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(syncIntervalMs));
    }
}

void updateHubReceiptTime() {
    if (!hubUpdatePending || !timeConfigured) return;

    timeval now;
    gettimeofday(&now, nullptr);
    if (now.tv_sec < 1577836800) return;

    const int64_t elapsedUs = esp_timer_get_time() - hubUpdateReceivedUs;
    const int64_t receivedUs = static_cast<int64_t>(now.tv_sec) * 1000000 +
                               now.tv_usec - elapsedUs;
    const time_t received = static_cast<time_t>(receivedUs / 1000000);
    tm localTime;
    char formatted[32];
    if (localtime_r(&received, &localTime) == nullptr ||
        strftime(formatted, sizeof(formatted), "Updated %H:%M %Z", &localTime) == 0) {
        Serial.println("Failed to format Hub update time");
        hubUpdatePending = false;
        return;
    }
    lv_label_set_text(orbs[0].contact, formatted);
    hubUpdatePending = false;
}

void recordBlynkUpdate() {
    hubUpdateReceivedUs = esp_timer_get_time();
    hubUpdatePending = true;
    lv_label_set_text(orbs[0].contact, "Updated --:--");
    updateHubReceiptTime();
}

void serviceTime() {
    if (!timeConfigured && WiFi.status() == WL_CONNECTED) {
        configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
        timeConfigured = true;
        timeConfigStarted = millis();
        Serial.println("Waiting for time synchronization");
    }
    if (hubUpdatePending) updateHubReceiptTime();
    if (timeConfigured && !timeWarningLogged &&
        time(nullptr) < 1577836800 && millis() - timeConfigStarted >= 60000) {
        Serial.println("Time synchronization not yet available");
        timeWarningLogged = true;
    }
}

void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
    const int pin = *static_cast<const int*>(driver->user_data);
    digitalWrite(pin, LOW);
    tft.pushImage(area->x1, area->y1, area->x2 - area->x1 + 1,
                  area->y2 - area->y1 + 1, &pixels->full);
    digitalWrite(pin, HIGH);
    lv_disp_flush_ready(driver);
}

void setStatusLabel(lv_obj_t* label, const char* status) {
    lv_color_t color = lv_color_white();
    if (strcmp(status, "Online") == 0) color = lv_color_hex(0x00ff00);
    else if (strcmp(status, "Offline") == 0) color = lv_color_hex(0xffa500);
    else if (strcmp(status, "Unknown") == 0) color = lv_color_hex(0xff0000);
    lv_label_set_text(label, status);
    lv_obj_set_style_text_color(label, color, 0);
}

void clearNodeStatuses() {
    for (size_t i = 1; i < displayCount; ++i) {
        setStatusLabel(orbs[i].status, "Unknown");
    }
}

void setHubToken(const char* text) {
    const bool valid = isValidHubToken(text);
    const char* nextToken = valid ? text : "";
    bool changed;
    portENTER_CRITICAL(&hubTokenMux);
    changed = strcmp(hubToken, nextToken) != 0;
    if (changed) {
        memcpy(hubToken, nextToken, strlen(nextToken) + 1);
        ++hubTokenGeneration;
    }
    portEXIT_CRITICAL(&hubTokenMux);

    if (!valid) {
        Serial.println("Hub token missing or invalid on V101; enter the Hub device token");
    } else if (changed) {
        Serial.println("Hub token received on V101");
    }
    if (changed || !valid) {
        hubPollStatus.setOnline(false);
        hubUpdatePending = false;
        clearNodeStatuses();
        if (hubPollTask) {
            lv_label_set_text(orbs[0].contact, valid ? "Waiting Hub data" : "Set Hub token");
            lv_obj_set_style_text_color(orbs[0].contact, lv_color_hex(0xffa500), 0);
            xTaskNotifyGive(hubPollTask);
        }
    }
}

void updateReading(uint16_t pin, const char* text);

void serviceHubResults() {
    if (!hubResults) return;
    HubPollResult result;
    if (xQueueReceive(hubResults, &result, 0) != pdTRUE) return;
    portENTER_CRITICAL(&hubTokenMux);
    const bool currentToken = hubToken[0] && result.tokenGeneration == hubTokenGeneration;
    portEXIT_CRITICAL(&hubTokenMux);
    if (!currentToken) return;
    hubPollStatus.setOnline(result.success);
    if (!result.success) {
        lv_label_set_text(orbs[0].contact, "Hub data stale");
        lv_obj_set_style_text_color(orbs[0].contact, lv_color_hex(0xffa500), 0);
        clearNodeStatuses();
        return;
    }

    clearNodeStatuses();
    for (size_t i = 0; i < hubDataPinCount; ++i) {
        if (result.readings.present & (uint32_t{1} << i)) {
            updateReading(hubDataPins[i], result.readings.values[i]);
        } else {
            Serial.printf("Missing or invalid Hub value on V%u\n",
                          static_cast<unsigned>(hubDataPins[i]));
        }
    }
    recordBlynkUpdate();
    const uint32_t allPresent = (uint32_t{1} << hubDataPinCount) - 1;
    if (result.readings.present != allPresent) {
        Serial.println("Hub data incomplete: missing or invalid datastream values");
        lv_label_set_text(orbs[0].contact, "Hub data incomplete");
        lv_obj_set_style_text_color(orbs[0].contact, lv_color_hex(0xffa500), 0);
    } else {
        lv_obj_set_style_text_color(orbs[0].contact, lv_color_white(), 0);
    }
}

void serviceHubPollStatus() {
    if (!Blynk.connected() || !hubPollStatus.pending()) return;
    const char* status = hubPollStatus.online() ? "Online" : "Offline";
    hubPollStatus.published();
    Blynk.virtualWrite(V99, status);
    Serial.printf("Hub polling status on V99: %s\n", status);
}

void drawTemperatureScale(lv_event_t* event) {
    const OrbDisplay& orb = *static_cast<const OrbDisplay*>(lv_event_get_user_data(event));
    lv_area_t area;
    lv_obj_get_coords(orb.screen, &area);
    const lv_coord_t width = lv_area_get_width(&area);
    const lv_coord_t height = lv_area_get_height(&area);
    const lv_point_t center = {
        static_cast<lv_coord_t>(area.x1 + width / 2),
        static_cast<lv_coord_t>(area.y1 + height / 2)
    };
    const uint16_t radius = (width < height ? width : height) / 2 - 2;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.width = 13;
    arc.rounded = false;

    constexpr int startAngle = 135;
    constexpr int segmentCount = 8;
    constexpr int sweep = 270;
    for (int i = 0; i < segmentCount; ++i) {
        const int start = i * sweep / segmentCount;
        const int end = (i + 1) * sweep / segmentCount - 3;
        arc.color = lv_color_hex(0x507927);
        lv_draw_arc(lv_event_get_draw_ctx(event), &arc, &center, radius,
                    startAngle + start, startAngle + end);
        const int activeEnd = orb.scaleDegrees < end ? orb.scaleDegrees : end;
        if (activeEnd > start) {
            arc.color = lv_color_hex(0xc8ff00);
            lv_draw_arc(lv_event_get_draw_ctx(event), &arc, &center, radius,
                        startAngle + start, startAngle + activeEnd);
        }
    }
}

lv_obj_t* createLabel(lv_obj_t* screen, const lv_font_t* font, int y, const char* text) {
    lv_obj_t* label = lv_label_create(screen);
    lv_obj_set_width(label, 178);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

bool parseNumber(const char* text, double& value) {
    errno = 0;
    char* end = nullptr;
    value = strtod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(value)) return false;
    while (isspace(static_cast<unsigned char>(*end))) ++end;
    return *end == '\0';
}

void updateReading(uint16_t pin, const char* text) {
    const OrbPin mapping = orbPinForVirtualPin(pin);
    if (mapping.field == OrbField::None) return;
    if (mapping.field == OrbField::Temperature) {
        Serial.printf("Temperature received on V%u: %s\n", static_cast<unsigned>(pin), text);
    }
    OrbDisplay& orb = orbs[mapping.orb];
    if (mapping.field == OrbField::LastContact) {
        if (mapping.orb == 0) return;
        const char* contactTime = text;
        const NodeStatus status = nodeStatusFromContact(text);
        switch (status) {
        case NodeStatus::Online:
            setStatusLabel(orb.status, "Online");
            if (strncmp(text, "ONLINE ", sizeof("ONLINE ") - 1) == 0)
                contactTime += sizeof("ONLINE ") - 1;
            break;
        case NodeStatus::Offline:
            setStatusLabel(orb.status, "Offline");
            contactTime += sizeof("OFFLINE ") - 1;
            break;
        case NodeStatus::Unknown:
            setStatusLabel(orb.status, "Unknown");
            if (strcmp(text, "--:--") != 0) {
                Serial.printf("Invalid node contact time on V%u: \"%.64s\"\n",
                              static_cast<unsigned>(pin), text);
            }
            break;
        }
        if (*text == '\0') {
            Serial.printf("Empty contact time on V%u\n", static_cast<unsigned>(pin));
            return;
        }
        lv_label_set_text_fmt(orb.contact, "Updated %s", contactTime);
        return;
    }

    double value;
    if (!parseNumber(text, value)) {
        Serial.printf("Invalid numeric value on V%u\n", static_cast<unsigned>(pin));
        return;
    }
    char formatted[32];
    switch (mapping.field) {
    case OrbField::Temperature: {
        snprintf(formatted, sizeof(formatted), "%.1f \xC2\xB0" "F", value);
        lv_label_set_text(orb.temperature, formatted);
        orb.scaleDegrees = temperatureScaleDegrees(value);
        lv_obj_invalidate(orb.screen);
        break;
    }
    case OrbField::Humidity:
        snprintf(formatted, sizeof(formatted), "%.1f %%", value);
        lv_label_set_text(orb.humidity, formatted);
        break;
    case OrbField::BatteryLevel: {
        if (value < 0 || value > 100 || value != floor(value)) {
            Serial.printf("Invalid battery percentage on V%u\n", static_cast<unsigned>(pin));
            return;
        }
        const int percentage = static_cast<int>(value);
        lv_label_set_text_fmt(orb.battery, "Battery %d%%", percentage);
        lv_obj_set_style_text_color(orb.battery,
                                    isLowBatteryPercentage(percentage) ? lv_color_hex(0xff0000) : lv_color_white(), 0);
        break;
    }
    default:
        break;
    }
}

void initializeDisplays() {
    for (int pin : displayPins) {
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
    }

    // Reset is shared, so initialize all controllers together exactly once.
    for (int pin : displayPins) {
        digitalWrite(pin, LOW);
    }
    tft.init();
    tft.setSwapBytes(true);
    for (int pin : displayPins) {
        digitalWrite(pin, HIGH);
    }

    lv_init();
    lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr, SCREEN_SIZE * drawBufferLines);
    for (size_t i = 0; i < displayCount; ++i) {
        lv_disp_drv_init(&displayDrivers[i]);
        displayDrivers[i].hor_res = TFT_WIDTH;
        displayDrivers[i].ver_res = TFT_HEIGHT;
        displayDrivers[i].flush_cb = flushDisplay;
        displayDrivers[i].draw_buf = &drawBuffer;
        displayDrivers[i].user_data = &displayPins[i];
        lv_disp_t* display = lv_disp_drv_register(&displayDrivers[i]);
        if (display == nullptr) {
            Serial.printf("Failed to register display %u\n", static_cast<unsigned>(i + 1));
            while (true) delay(1000);
        }

        OrbDisplay& orb = orbs[i];
        orb.screen = lv_disp_get_scr_act(display);
        lv_obj_set_style_bg_color(orb.screen, lv_color_hex(0x244739), 0);
        lv_obj_set_style_text_color(orb.screen, lv_color_white(), 0);
        lv_obj_add_event_cb(orb.screen, drawTemperatureScale, LV_EVENT_DRAW_MAIN, &orb);

        char title[12];
        if (i == 0) {
            snprintf(title, sizeof(title), "Hub");
        } else {
            snprintf(title, sizeof(title), "Node %u", static_cast<unsigned>(i));
        }
        createLabel(orb.screen, &lv_font_montserrat_14, 33, title);
        orb.temperature = createLabel(orb.screen, &lv_font_montserrat_40, 55, "--.- \xC2\xB0" "F");
        orb.humidity = createLabel(orb.screen, &lv_font_montserrat_28, 105, "--.- %");
        createLabel(orb.screen, &lv_font_montserrat_14, 137, "HUMIDITY");
        if (i != 0) {
            orb.battery = createLabel(orb.screen, &lv_font_montserrat_14, 154, "Battery --%");
        }
        orb.contact = createLabel(orb.screen, &lv_font_montserrat_14, 172, "Updated --:--");
        orb.status = createLabel(orb.screen, &lv_font_montserrat_14, 193,
                                 i == 0 ? "Starting..." : "Unknown");
        if (i != 0) setStatusLabel(orb.status, "Unknown");
    }
}
void serviceDisplay() {
    const char* status;
    switch (BlynkEdgent.getState()) {
    case Edgent::MODE_IDLE: status = "Setup timed out"; break;
    case Edgent::MODE_WAIT_CONFIG: status = "Add in Blynk app"; break;
    case Edgent::MODE_CONNECTING_NET: status = "Connecting Wi-Fi"; break;
    case Edgent::MODE_CONNECTING_CLOUD: status = "Connecting Blynk"; break;
    case Edgent::MODE_RUNNING: status = Blynk.connected() ? "Online" : "Connecting Blynk"; break;
    case Edgent::MODE_OTA_UPGRADE: status = "Updating..."; break;
    case Edgent::MODE_RESET_CONFIG: status = "Resetting setup"; break;
    default: status = "Provisioning error"; break;
    }
    Serial.println(status);
    setStatusLabel(orbs[0].status, status);
}

} // namespace

BLYNK_CONNECTED() {
    Serial.println("Blynk connected");
    hubPollStatus.onConnected();
    Blynk.syncVirtual(V101);
}

BLYNK_WRITE(V101) {
    setHubToken(param.asStr());
}

BLYNK_WRITE_DEFAULT() {}

void setup() {
    Serial.begin(115200);
    initializeDisplays();
    lv_timer_handler();

    if (!validHubServer()) {
        Serial.println("Set a valid HUB_BLYNK_SERVER build flag");
        lv_label_set_text(orbs[0].contact, "Set Hub server");
    } else {
        Serial.println("Waiting for Hub device token on V101");
        lv_label_set_text(orbs[0].contact, "Set Hub token");
        lv_obj_set_style_text_color(orbs[0].contact, lv_color_hex(0xffa500), 0);
        hubResults = xQueueCreate(1, sizeof(HubPollResult));
        if (!hubResults || xTaskCreate(pollHubTask, "HubPoll", 12288, nullptr, 1, &hubPollTask) != pdPASS) {
            Serial.println("Failed to start Hub polling task");
            lv_label_set_text(orbs[0].contact, "Hub poll error");
        }
    }

    if (!String(BLYNK_TEMPLATE_ID).startsWith("TMPL") ||
        strlen(BLYNK_TEMPLATE_NAME) == 0) {
        Serial.println("Set a valid Blynk template ID and name before provisioning.");
        setStatusLabel(orbs[0].status, "Set template ID");
        return;
    }
    BlynkEdgent.setConfigTimeout(10 * 60);
    BlynkEdgent.setConfigSkipLimit(0);
    interaction.begin();
    BlynkEdgent.onStateChange([]() {
        BLYNK_LOG("State: %s", BlynkEdgent.getStateName());
        interaction.updateIndicator();
        serviceDisplay();
    });
    edgentStarted = BlynkEdgent.begin();
    if (!edgentStarted) {
        Serial.println("Failed to start Blynk Edgent");
        setStatusLabel(orbs[0].status, "Blynk setup error");
        return;
    }
    BlynkEdgent.initConsole(Serial);
}

void loop() {
    if (edgentStarted) {
        BlynkEdgent.run();
        interaction.run();
        const bool connected = Blynk.connected();
        if (connected != wasBlynkConnected) {
            if (!connected) clearNodeStatuses();
            serviceDisplay();
            wasBlynkConnected = connected;
        }
    }
    serviceTime();
    serviceHubResults();
    serviceHubPollStatus();
    lv_timer_handler();
    delay(5);
}

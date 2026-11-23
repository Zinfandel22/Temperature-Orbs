#include <Arduino.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
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
#include "OrbData.h"
#include <TFT_eSPI.h>
#include <lvgl.h>

namespace {
constexpr size_t displayCount = 5;
int displayPins[displayCount] = {
    SCREEN_1_CS, SCREEN_2_CS, SCREEN_3_CS, SCREEN_4_CS, SCREEN_5_CS
};

TFT_eSPI tft;
lv_color_t drawPixels[SCREEN_SIZE * 20];
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
    char batteryLevel[8];
    char batteryVoltage[16];
};

OrbDisplay orbs[displayCount];
bool edgentStarted = false;

void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
    const int pin = *static_cast<const int*>(driver->user_data);
    digitalWrite(pin, LOW);
    tft.pushImage(area->x1, area->y1, area->x2 - area->x1 + 1,
                  area->y2 - area->y1 + 1, &pixels->full);
    digitalWrite(pin, HIGH);
    lv_disp_flush_ready(driver);
}

void setStatus(const char* status) {
    for (OrbDisplay& orb : orbs) {
        lv_label_set_text(orb.status, status);
    }
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

void updateBatteryLabel(OrbDisplay& orb) {
    lv_label_set_text_fmt(orb.battery, "Battery %s  %s", orb.batteryLevel, orb.batteryVoltage);
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
    OrbDisplay& orb = orbs[mapping.orb];
    if (mapping.field == OrbField::LastContact) {
        if (*text == '\0') {
            Serial.printf("Empty contact time on V%u\n", static_cast<unsigned>(pin));
            return;
        }
        lv_label_set_text_fmt(orb.contact, "Last %s", text);
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
        const double fahrenheit = fahrenheitFromCelsius(value);
        if (!isfinite(fahrenheit)) {
            Serial.printf("Temperature out of range on V%u\n", static_cast<unsigned>(pin));
            return;
        }
        snprintf(formatted, sizeof(formatted), "%.1f F", fahrenheit);
        lv_label_set_text(orb.temperature, formatted);
        orb.scaleDegrees = temperatureScaleDegrees(fahrenheit);
        lv_obj_invalidate(orb.screen);
        break;
    }
    case OrbField::Humidity:
        snprintf(formatted, sizeof(formatted), "%.1f %%", value);
        lv_label_set_text(orb.humidity, formatted);
        break;
    case OrbField::BatteryVoltage:
        snprintf(orb.batteryVoltage, sizeof(orb.batteryVoltage), "%.2f V", value);
        updateBatteryLabel(orb);
        break;
    case OrbField::BatteryLevel:
        if (value < 0 || value > 100 || value != floor(value)) {
            Serial.printf("Invalid battery percentage on V%u\n", static_cast<unsigned>(pin));
            return;
        }
        snprintf(orb.batteryLevel, sizeof(orb.batteryLevel), "%d%%", static_cast<int>(value));
        updateBatteryLabel(orb);
        break;
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
    lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr, SCREEN_SIZE * 20);
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
        orb.temperature = createLabel(orb.screen, &lv_font_montserrat_40, 55, "--.- F");
        orb.humidity = createLabel(orb.screen, &lv_font_montserrat_28, 105, "--.- %");
        createLabel(orb.screen, &lv_font_montserrat_14, 137, "HUMIDITY");
        if (i != 0) {
            snprintf(orb.batteryLevel, sizeof(orb.batteryLevel), "--%%");
            snprintf(orb.batteryVoltage, sizeof(orb.batteryVoltage), "-- V");
            orb.battery = createLabel(orb.screen, &lv_font_montserrat_14, 154, "");
            updateBatteryLabel(orb);
        }
        orb.contact = createLabel(orb.screen, &lv_font_montserrat_14, 172, "Last --:--");
        orb.status = createLabel(orb.screen, &lv_font_montserrat_14, 193, "Starting...");
    }
}
void serviceDisplay() {
    const char* status;
    switch (BlynkEdgent.getState()) {
    case Edgent::MODE_IDLE: status = "Setup timed out"; break;
    case Edgent::MODE_WAIT_CONFIG: status = "Add in Blynk app"; break;
    case Edgent::MODE_CONNECTING_NET: status = "Connecting Wi-Fi"; break;
    case Edgent::MODE_CONNECTING_CLOUD: status = "Connecting Blynk"; break;
    case Edgent::MODE_RUNNING: status = "Online"; break;
    case Edgent::MODE_OTA_UPGRADE: status = "Updating..."; break;
    case Edgent::MODE_RESET_CONFIG: status = "Resetting setup"; break;
    default: status = "Provisioning error"; break;
    }
    Serial.println(status);
    setStatus(status);
}
} // namespace

BLYNK_CONNECTED() {
    Serial.println("Blynk connected; syncing hub and node readings");
    Blynk.syncVirtual(V0, V1, V2, V3, V4, V5, V6, V7, V8, V9,
                      V10, V11, V12, V13, V14, V20, V21, V22, V23,
                      V30, V31, V32, V33);
}

BLYNK_WRITE_DEFAULT() {
    updateReading(request.pin, param.asStr());
}

void setup() {
    Serial.begin(115200);
    initializeDisplays();
    lv_timer_handler();

    if (!String(BLYNK_TEMPLATE_ID).startsWith("TMPL") ||
        strlen(BLYNK_TEMPLATE_NAME) == 0) {
        Serial.println("Set a valid Blynk template ID and name before provisioning.");
        setStatus("Set template ID");
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
        setStatus("Blynk setup error");
        return;
    }
    BlynkEdgent.initConsole(Serial);
}

void loop() {
    if (edgentStarted) {
        BlynkEdgent.run();
        interaction.run();
    }
    lv_timer_handler();
    delay(5);
}

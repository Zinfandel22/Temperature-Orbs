#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "OrbData.h"

constexpr uint8_t hubDataPins[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 30, 31, 32, 33
};
constexpr size_t hubDataPinCount = sizeof(hubDataPins) / sizeof(hubDataPins[0]);
constexpr size_t hubDataValueSize = 64;

struct HubReadings {
    char values[hubDataPinCount][hubDataValueSize];
    uint32_t present;
};

inline bool decodeHubReadings(JsonObjectConst object, HubReadings& readings) {
    readings.present = 0;
    for (size_t i = 0; i < hubDataPinCount; ++i) {
        char key[5];
        snprintf(key, sizeof(key), "v%u", static_cast<unsigned>(hubDataPins[i]));
        JsonVariantConst value = object[key];
        const OrbField field = orbPinForVirtualPin(hubDataPins[i]).field;
        if (field == OrbField::LastContact) {
            const char* text = value.as<const char*>();
            if (!text || strlen(text) >= hubDataValueSize) continue;
            memcpy(readings.values[i], text, strlen(text) + 1);
        } else {
            if (!value.is<double>()) continue;
            const double number = value.as<double>();
            if (!isfinite(number) ||
                (field == OrbField::BatteryLevel &&
                 (number < 0 || number > 100 || number != floor(number)))) continue;
            const int length = snprintf(readings.values[i], hubDataValueSize, "%.17g",
                                        number);
            if (length < 0 || static_cast<size_t>(length) >= hubDataValueSize) continue;
        }
        readings.present |= uint32_t{1} << i;
    }
    return readings.present != 0;
}

#pragma once

#include <stdint.h>

enum class OrbField {
    None,
    Temperature,
    Humidity,
    LastContact,
    BatteryLevel
};

struct OrbPin {
    uint8_t orb;
    OrbField field;
};

enum class NodeStatus {
    Unknown,
    Online,
    Offline
};

constexpr bool isContactTime(const char* text) {
    return text[0] >= '0' && text[0] <= '9' &&
           text[1] >= '0' && text[1] <= '9' &&
           text[2] == ':' &&
           text[3] >= '0' && text[3] <= '5' &&
           text[4] >= '0' && text[4] <= '9' &&
           text[5] == '\0';
}

constexpr NodeStatus nodeStatusFromContact(const char* text) {
    if (isContactTime(text)) return NodeStatus::Online;
    const char prefix[] = "OFFLINE ";
    for (unsigned i = 0; i < sizeof(prefix) - 1; ++i) {
        if (text[i] != prefix[i]) return NodeStatus::Unknown;
    }
    return isContactTime(text + sizeof(prefix) - 1) ?
           NodeStatus::Offline : NodeStatus::Unknown;
}

constexpr OrbPin orbPinForVirtualPin(uint16_t pin) {
    if (pin <= 3) return {static_cast<uint8_t>(pin + 1), OrbField::Temperature};
    if (pin <= 7) return {static_cast<uint8_t>(pin - 3), OrbField::Humidity};
    if (pin == 8) return {0, OrbField::Temperature};
    if (pin == 9) return {0, OrbField::Humidity};
    if (pin <= 13) return {static_cast<uint8_t>(pin - 9), OrbField::LastContact};
    if (pin == 14) return {0, OrbField::LastContact};
    if (pin >= 30 && pin <= 33) return {static_cast<uint8_t>(pin - 29), OrbField::BatteryLevel};
    return {0, OrbField::None};
}

constexpr double fahrenheitFromCelsius(double celsius) {
    return celsius * 9.0 / 5.0 + 32.0;
}

constexpr bool isLowBatteryPercentage(int percentage) {
    return percentage < 10;
}

constexpr int temperatureScaleDegrees(double fahrenheit) {
    return fahrenheit <= 30.0 ? 0 :
           fahrenheit >= 110.0 ? 270 :
           static_cast<int>((fahrenheit - 30.0) * 270.0 / 80.0);
}

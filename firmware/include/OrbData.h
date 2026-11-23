#pragma once

#include <stdint.h>

enum class OrbField {
    None,
    Temperature,
    Humidity,
    LastContact,
    BatteryVoltage,
    BatteryLevel
};

struct OrbPin {
    uint8_t orb;
    OrbField field;
};

constexpr OrbPin orbPinForVirtualPin(uint16_t pin) {
    if (pin <= 3) return {static_cast<uint8_t>(pin + 1), OrbField::Temperature};
    if (pin <= 7) return {static_cast<uint8_t>(pin - 3), OrbField::Humidity};
    if (pin == 8) return {0, OrbField::Temperature};
    if (pin == 9) return {0, OrbField::Humidity};
    if (pin <= 13) return {static_cast<uint8_t>(pin - 9), OrbField::LastContact};
    if (pin == 14) return {0, OrbField::LastContact};
    if (pin >= 20 && pin <= 23) return {static_cast<uint8_t>(pin - 19), OrbField::BatteryVoltage};
    if (pin >= 30 && pin <= 33) return {static_cast<uint8_t>(pin - 29), OrbField::BatteryLevel};
    return {0, OrbField::None};
}

constexpr double fahrenheitFromCelsius(double celsius) {
    return celsius * 9.0 / 5.0 + 32.0;
}

constexpr int temperatureScaleDegrees(double fahrenheit) {
    return fahrenheit <= 30.0 ? 0 :
           fahrenheit >= 110.0 ? 270 :
           static_cast<int>((fahrenheit - 30.0) * 270.0 / 80.0);
}

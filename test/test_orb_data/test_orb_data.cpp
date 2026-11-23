#include "OrbData.h"

constexpr bool hasMapping(uint16_t pin, uint8_t orb, OrbField field) {
    return orbPinForVirtualPin(pin).orb == orb &&
           orbPinForVirtualPin(pin).field == field;
}

constexpr bool allNodeMappings() {
    for (uint16_t node = 0; node < 4; ++node) {
        const uint8_t orb = static_cast<uint8_t>(node + 1);
        if (!hasMapping(node, orb, OrbField::Temperature) ||
            !hasMapping(node + 4, orb, OrbField::Humidity) ||
            !hasMapping(node + 10, orb, OrbField::LastContact) ||
            !hasMapping(node + 20, orb, OrbField::BatteryVoltage) ||
            !hasMapping(node + 30, orb, OrbField::BatteryLevel)) return false;
    }
    return true;
}

static_assert(allNodeMappings(), "all node datastreams");
static_assert(hasMapping(8, 0, OrbField::Temperature), "hub temperature");
static_assert(hasMapping(9, 0, OrbField::Humidity), "hub humidity");
static_assert(hasMapping(14, 0, OrbField::LastContact), "hub contact");
static_assert(hasMapping(0, 1, OrbField::Temperature), "node 1 temperature");
static_assert(hasMapping(3, 4, OrbField::Temperature), "node 4 temperature");
static_assert(hasMapping(4, 1, OrbField::Humidity), "node 1 humidity");
static_assert(hasMapping(7, 4, OrbField::Humidity), "node 4 humidity");
static_assert(hasMapping(10, 1, OrbField::LastContact), "node 1 contact");
static_assert(hasMapping(13, 4, OrbField::LastContact), "node 4 contact");
static_assert(hasMapping(20, 1, OrbField::BatteryVoltage), "node 1 voltage");
static_assert(hasMapping(23, 4, OrbField::BatteryVoltage), "node 4 voltage");
static_assert(hasMapping(30, 1, OrbField::BatteryLevel), "node 1 battery");
static_assert(hasMapping(33, 4, OrbField::BatteryLevel), "node 4 battery");
static_assert(hasMapping(15, 0, OrbField::None), "unused pin");
static_assert(hasMapping(24, 0, OrbField::None), "unused voltage pin");
static_assert(hasMapping(34, 0, OrbField::None), "unused battery pin");
static_assert(fahrenheitFromCelsius(0) == 32, "freezing point");
static_assert(fahrenheitFromCelsius(100) == 212, "boiling point");
static_assert(temperatureScaleDegrees(20) == 0, "clamp below 30 F");
static_assert(temperatureScaleDegrees(30) == 0, "scale minimum");
static_assert(temperatureScaleDegrees(70) == 135, "scale midpoint");
static_assert(temperatureScaleDegrees(110) == 270, "scale maximum");
static_assert(temperatureScaleDegrees(120) == 270, "clamp above 110 F");

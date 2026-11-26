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

class HubPollStatus {
public:
    constexpr void setOnline(bool online) {
        if (online_ != online) {
            online_ = online;
            pending_ = true;
        }
    }

    constexpr void onConnected() { pending_ = true; }
    constexpr bool online() const { return online_; }
    constexpr bool pending() const { return pending_; }
    constexpr void published() { pending_ = false; }

private:
    bool online_ = false;
    bool pending_ = true;
};

constexpr bool isValidHubToken(const char* token) {
    if (!token) return false;
    unsigned length = 0;
    while (token[length]) {
        const char c = token[length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_') ||
            length >= 32) return false;
        ++length;
    }
    return length == 32;
}

constexpr bool isContactTime(const char* text) {
    return text[0] >= '0' && text[0] <= '9' &&
           text[1] >= '0' && text[1] <= '9' &&
           text[2] == ':' &&
           text[3] >= '0' && text[3] <= '5' &&
           text[4] >= '0' && text[4] <= '9' &&
           text[5] == '\0';
}

constexpr bool isContactClockTime(const char* text) {
    if (text[0] < '0' || text[0] > '9') return false;
    unsigned hour = text[0] - '0';
    unsigned i = 1;
    if (text[i] >= '0' && text[i] <= '9') {
        hour = hour * 10 + text[i] - '0';
        ++i;
    }
    return hour >= 1 && hour <= 12 &&
           text[i] == ':' &&
           text[i + 1] >= '0' && text[i + 1] <= '5' &&
           text[i + 2] >= '0' && text[i + 2] <= '9' &&
           text[i + 3] == ' ' &&
           (text[i + 4] == 'A' || text[i + 4] == 'P') &&
           text[i + 5] == 'M' && text[i + 6] == '\0';
}

constexpr NodeStatus nodeStatusFromContact(const char* text) {
    if (isContactTime(text)) return NodeStatus::Online;
    const char online[] = "ONLINE ";
    const char offline[] = "OFFLINE ";
    const bool isOnline = text[0] == 'O' && text[1] == 'N';
    const char* prefix = isOnline ? online : offline;
    unsigned i = 0;
    for (; prefix[i]; ++i) {
        if (text[i] != prefix[i]) return NodeStatus::Unknown;
    }
    if (!isContactTime(text + i) && !isContactClockTime(text + i))
        return NodeStatus::Unknown;
    return isOnline ? NodeStatus::Online : NodeStatus::Offline;
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

constexpr bool isLowBatteryPercentage(int percentage) {
    return percentage < 10;
}

constexpr int temperatureScaleDegrees(double fahrenheit) {
    return fahrenheit <= 30.0 ? 0 :
           fahrenheit >= 110.0 ? 270 :
           static_cast<int>((fahrenheit - 30.0) * 270.0 / 80.0);
}

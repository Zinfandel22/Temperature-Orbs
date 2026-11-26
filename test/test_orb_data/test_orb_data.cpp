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
static_assert(hasMapping(20, 0, OrbField::None), "node 1 voltage is not displayed");
static_assert(hasMapping(23, 0, OrbField::None), "node 4 voltage is not displayed");
static_assert(hasMapping(30, 1, OrbField::BatteryLevel), "node 1 battery");
static_assert(hasMapping(33, 4, OrbField::BatteryLevel), "node 4 battery");
static_assert(hasMapping(15, 0, OrbField::None), "unused pin");
static_assert(hasMapping(24, 0, OrbField::None), "unused voltage pin");
static_assert(hasMapping(34, 0, OrbField::None), "unused battery pin");
static_assert(isLowBatteryPercentage(0), "empty battery");
static_assert(isLowBatteryPercentage(9), "below low battery threshold");
static_assert(!isLowBatteryPercentage(10), "at low battery threshold");
static_assert(!isLowBatteryPercentage(100), "full battery");
static_assert(temperatureScaleDegrees(20) == 0, "clamp below 30 F");
static_assert(temperatureScaleDegrees(30) == 0, "scale minimum");
static_assert(temperatureScaleDegrees(70) == 135, "scale midpoint");
static_assert(temperatureScaleDegrees(110) == 270, "scale maximum");
static_assert(temperatureScaleDegrees(120) == 270, "clamp above 110 F");
static_assert(isValidHubToken("0123456789abcdefghijABCDEFGHIJ-_"), "valid Hub token");
static_assert(!isValidHubToken(nullptr), "null Hub token");
static_assert(!isValidHubToken(""), "empty Hub token");
static_assert(!isValidHubToken("0123456789abcdefghijABCDEFGHIJ-"), "short Hub token");
static_assert(!isValidHubToken("0123456789abcdefghijABCDEFGHIJ-__"), "long Hub token");
static_assert(!isValidHubToken("0123456789abcdefghijABCDEFGHIJ &"), "invalid Hub token characters");
constexpr bool hubPollStatusTransitions() {
    HubPollStatus status;
    if (status.online() || !status.pending()) return false;
    status.published();
    status.setOnline(false);
    if (status.pending()) return false;
    status.setOnline(true);
    if (!status.online() || !status.pending()) return false;
    status.published();
    status.onConnected();
    if (!status.pending() || !status.online()) return false;
    status.published();
    status.setOnline(false);
    return !status.online() && status.pending();
}
static_assert(hubPollStatusTransitions(), "poll status transitions and reconnect");
static_assert(nodeStatusFromContact("00:00") == NodeStatus::Online, "online after a report");
static_assert(nodeStatusFromContact("01:29") == NodeStatus::Online, "online before timeout");
static_assert(nodeStatusFromContact("OFFLINE 01:30") == NodeStatus::Offline, "offline at timeout");
static_assert(nodeStatusFromContact("OFFLINE 99:59") == NodeStatus::Offline, "capped elapsed time");
static_assert(nodeStatusFromContact("ONLINE 10:06 PM") == NodeStatus::Online, "online clock time");
static_assert(nodeStatusFromContact("ONLINE 1:05 AM") == NodeStatus::Online, "single-digit hour");
static_assert(nodeStatusFromContact("OFFLINE 12:00 AM") == NodeStatus::Offline, "offline clock time");
static_assert(nodeStatusFromContact("ONLINE 00:06 PM") == NodeStatus::Unknown, "invalid clock hour");
static_assert(nodeStatusFromContact("ONLINE 13:06 PM") == NodeStatus::Unknown, "invalid clock hour");
static_assert(nodeStatusFromContact("ONLINE 10:60 PM") == NodeStatus::Unknown, "invalid clock minutes");
static_assert(nodeStatusFromContact("ONLINE 10:06 XM") == NodeStatus::Unknown, "invalid clock suffix");
static_assert(nodeStatusFromContact("ONLINE 10:06 P") == NodeStatus::Unknown, "truncated clock suffix");
static_assert(nodeStatusFromContact("ONLINE 10:06 PM extra") == NodeStatus::Unknown, "extra clock suffix");
static_assert(nodeStatusFromContact("--:--") == NodeStatus::Unknown, "no contact yet");
static_assert(nodeStatusFromContact("") == NodeStatus::Unknown, "empty contact");
static_assert(nodeStatusFromContact("OFFLINE") == NodeStatus::Unknown, "missing offline time");
static_assert(nodeStatusFromContact("OFFLINE 100:00") == NodeStatus::Unknown, "hours exceed cap");
static_assert(nodeStatusFromContact("12:60") == NodeStatus::Unknown, "invalid minutes");
static_assert(nodeStatusFromContact("12:34 extra") == NodeStatus::Unknown, "unexpected suffix");
static_assert(nodeStatusFromContact("offline 12:34") == NodeStatus::Unknown, "unexpected format");

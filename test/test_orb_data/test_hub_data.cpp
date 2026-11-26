#include "HubData.h"
#include <assert.h>

int main() {
    JsonDocument document;
    assert(!deserializeJson(document, R"({"v0":0,"v8":32.5,"v10":"OFFLINE 01:30","v30":9})"));
    HubReadings readings = {};
    assert(decodeHubReadings(document.as<JsonObjectConst>(), readings));
    assert(strcmp(readings.values[0], "0") == 0);
    assert(strcmp(readings.values[8], "32.5") == 0);
    assert(strcmp(readings.values[10], "OFFLINE 01:30") == 0);
    assert(strcmp(readings.values[14], "9") == 0);
    assert((readings.present & (uint32_t{1} << 1)) == 0);

    document.clear();
    assert(!deserializeJson(document, R"({"v0":"72","v1":null,"v10":42,"v30":9.5,"v31":101})"));
    assert(!decodeHubReadings(document.as<JsonObjectConst>(), readings));
    assert(readings.present == 0);

    document.clear();
    assert(!deserializeJson(document, R"({"v10":"0123456789012345678901234567890123456789012345678901234567890123"})"));
    assert(!decodeHubReadings(document.as<JsonObjectConst>(), readings));
}

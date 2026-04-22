#pragma once

#include "esp_err.h"

namespace lgac {

enum class Power : uint8_t {
    Off = 0,
    On = 1,
};

enum class Mode : uint8_t {
    Auto = 0,
    Cool = 1,
    Dry = 2,
    Fan = 3,
    Heat = 4,
};

enum class Fan : uint8_t {
    Auto = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Powerful = 4,
    Natural = 5,
};

struct State {
    Power power = Power::Off;
    Mode mode = Mode::Cool;
    Fan fan = Fan::Auto;
    uint8_t temperature_c = 24;
};

esp_err_t init();
esp_err_t send(const State &state);
State &state();

} // namespace lgac


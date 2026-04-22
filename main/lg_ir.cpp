#include "lg_ir.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <inttypes.h>

#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "sdkconfig.h"

namespace {

constexpr char TAG[] = "lg_ir";
constexpr uint32_t kResolutionHz = 1000000;
constexpr uint16_t kHeaderMarkUs = 8000;
constexpr uint16_t kHeaderSpaceUs = 4000;
constexpr uint16_t kBitMarkUs = 560;
constexpr uint16_t kOneSpaceUs = 1690;
constexpr uint16_t kZeroSpaceUs = 560;
constexpr uint16_t kFooterMarkUs = 560;
constexpr uint8_t kLgBits = 28;

rmt_channel_handle_t s_channel = nullptr;
rmt_encoder_handle_t s_copy_encoder = nullptr;
lgac::State s_state = {
    .power = lgac::Power::Off,
    .mode = lgac::Mode::Cool,
    .fan = static_cast<lgac::Fan>(CONFIG_LG_IR_DEFAULT_FAN),
    .temperature_c = CONFIG_LG_IR_DEFAULT_TEMP_C,
};

static uint8_t nibble_sum_checksum(uint32_t frame_without_checksum)
{
    uint8_t sum = 0;
    for (int shift = 4; shift < 28; shift += 4) {
        sum += (frame_without_checksum >> shift) & 0x0f;
    }
    return sum & 0x0f;
}

static uint8_t encode_mode(lgac::Mode mode)
{
    switch (mode) {
    case lgac::Mode::Auto:
        return 0;
    case lgac::Mode::Cool:
        return 0;
    case lgac::Mode::Dry:
        return 1;
    case lgac::Mode::Fan:
        return 2;
    case lgac::Mode::Heat:
        return 4;
    }
    return 0;
}

static uint8_t encode_fan(lgac::Fan fan)
{
    switch (fan) {
    case lgac::Fan::Auto:
        return 0;
    case lgac::Fan::Low:
        return 2;
    case lgac::Fan::Medium:
        return 4;
    case lgac::Fan::High:
        return 5;
    case lgac::Fan::Powerful:
        return 6;
    case lgac::Fan::Natural:
        return 7;
    }
    return 0;
}

static uint32_t build_lg_28bit_frame(const lgac::State &state)
{
    if (state.power == lgac::Power::Off) {
        return 0x88C0051;
    }

    const uint8_t temp = std::clamp<uint8_t>(state.temperature_c, 18, 30);
    const uint8_t temp_field = temp - 15;
    const uint8_t mode = encode_mode(state.mode);
    const uint8_t fan = encode_fan(state.fan);

    uint32_t frame = 0;
    frame |= 0x88u << 20;
    frame |= 0x0u << 16;
    frame |= (mode & 0x07u) << 13;
    frame |= (temp_field & 0x0fu) << 8;
    frame |= (fan & 0x07u) << 4;
    frame |= nibble_sum_checksum(frame);
    return frame;
}

static rmt_symbol_word_t symbol(uint16_t high_us, uint16_t low_us)
{
    return {
        .duration0 = high_us,
        .level0 = 1,
        .duration1 = low_us,
        .level1 = 0,
    };
}

static size_t encode_symbols(uint32_t frame, std::array<rmt_symbol_word_t, kLgBits + 2> &symbols)
{
    size_t idx = 0;
    symbols[idx++] = symbol(kHeaderMarkUs, kHeaderSpaceUs);

    for (int bit = kLgBits - 1; bit >= 0; --bit) {
        const bool one = (frame >> bit) & 0x01;
        symbols[idx++] = symbol(kBitMarkUs, one ? kOneSpaceUs : kZeroSpaceUs);
    }

    symbols[idx++] = symbol(kFooterMarkUs, 0);
    return idx;
}

} // namespace

namespace lgac {

State &state()
{
    return s_state;
}

esp_err_t init()
{
    if (s_channel != nullptr) {
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_config = {
        .gpio_num = static_cast<gpio_num_t>(CONFIG_LG_IR_TX_GPIO),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = kResolutionHz,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_config, &s_channel), TAG, "create RMT TX channel");

    rmt_carrier_config_t carrier_config = {
        .frequency_hz = CONFIG_LG_IR_CARRIER_HZ,
        .duty_cycle = 0.33f,
        .flags = {
            .polarity_active_low = false,
        },
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(s_channel, &carrier_config), TAG, "apply carrier");

    rmt_copy_encoder_config_t encoder_config = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encoder_config, &s_copy_encoder), TAG, "create copy encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_channel), TAG, "enable RMT channel");

    ESP_LOGI(TAG, "IR transmitter ready on GPIO%d at %d Hz", CONFIG_LG_IR_TX_GPIO, CONFIG_LG_IR_CARRIER_HZ);
    return ESP_OK;
}

esp_err_t send(const State &new_state)
{
    s_state = new_state;
    s_state.temperature_c = std::clamp<uint8_t>(s_state.temperature_c, 18, 30);

    const uint32_t frame = build_lg_28bit_frame(s_state);
    std::array<rmt_symbol_word_t, kLgBits + 2> symbols = {};
    const size_t symbol_count = encode_symbols(frame, symbols);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    ESP_LOGI(TAG, "send LG frame=0x%07" PRIX32 " power=%u mode=%u fan=%u temp=%u",
             frame, static_cast<unsigned>(s_state.power), static_cast<unsigned>(s_state.mode),
             static_cast<unsigned>(s_state.fan), s_state.temperature_c);

    ESP_RETURN_ON_ERROR(rmt_transmit(s_channel, s_copy_encoder, symbols.data(),
                                     symbol_count * sizeof(rmt_symbol_word_t), &tx_config),
                        TAG, "transmit LG frame");
    return rmt_tx_wait_all_done(s_channel, pdMS_TO_TICKS(200));
}

} // namespace lgac

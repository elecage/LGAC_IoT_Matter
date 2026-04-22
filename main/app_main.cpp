#include <algorithm>
#include <cstdlib>
#include <cstdint>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "esp_matter.h"
#include "esp_matter_cluster.h"
#include "esp_matter_console.h"
#include "esp_matter_endpoint.h"

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <lib/core/CHIPError.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/ESP32Config.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "lg_ir.h"
#include "sdkconfig.h"

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

void emberAfPluginLevelControlCoupledColorTempChangeCallback(uint16_t)
{
}

namespace {

constexpr char TAG[] = "app_main";
constexpr uint16_t kCommissioningTimeoutSeconds = 300;
constexpr uint16_t kSetupDiscriminator = 0x0A55;
constexpr int8_t kWifiTxPowerQuarterDbm = 40; // 10 dBm, units are 0.25 dBm.
uint16_t s_thermostat_endpoint_id = 0;

static void abort_on_failure(bool condition, const char *message)
{
    if (!condition) {
        ESP_LOGE(TAG, "%s", message);
        abort();
    }
}

static uint8_t matter_system_mode_to_lg(uint8_t system_mode)
{
    switch (system_mode) {
    case 0: // Off
        return static_cast<uint8_t>(lgac::Mode::Cool);
    case 3: // Cool
        return static_cast<uint8_t>(lgac::Mode::Cool);
    case 4: // Heat
        return static_cast<uint8_t>(lgac::Mode::Heat);
    case 7: // FanOnly
        return static_cast<uint8_t>(lgac::Mode::Fan);
    case 8: // Dry
        return static_cast<uint8_t>(lgac::Mode::Dry);
    case 1: // Auto
    default:
        return static_cast<uint8_t>(lgac::Mode::Auto);
    }
}

static lgac::Fan matter_fan_mode_to_lg(uint8_t fan_mode)
{
    switch (fan_mode) {
    case 1:
        return lgac::Fan::Low;
    case 2:
        return lgac::Fan::Medium;
    case 3:
    case 4:
        return lgac::Fan::High;
    case 5:
        return lgac::Fan::Auto;
    default:
        return lgac::Fan::Auto;
    }
}

static void update_attribute(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    attribute_t *attribute = attribute::get(s_thermostat_endpoint_id, cluster_id, attribute_id);
    if (attribute != nullptr) {
        attribute::update(s_thermostat_endpoint_id, cluster_id, attribute_id, val);
    }
}

static esp_err_t sync_ir_from_matter(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    lgac::State next = lgac::state();

    if (cluster_id == Thermostat::Id) {
        if (attribute_id == Thermostat::Attributes::SystemMode::Id) {
            const uint8_t system_mode = val->val.u8;
            next.power = (system_mode == 0) ? lgac::Power::Off : lgac::Power::On;
            next.mode = static_cast<lgac::Mode>(matter_system_mode_to_lg(system_mode));
        } else if (attribute_id == Thermostat::Attributes::OccupiedCoolingSetpoint::Id ||
                   attribute_id == Thermostat::Attributes::OccupiedHeatingSetpoint::Id) {
            const int16_t centi_c = val->val.i16;
            next.temperature_c = static_cast<uint8_t>(std::clamp<int16_t>(centi_c / 100, 18, 30));
        } else {
            return ESP_OK;
        }
    } else if (cluster_id == FanControl::Id) {
        if (attribute_id == FanControl::Attributes::FanMode::Id) {
            next.fan = matter_fan_mode_to_lg(val->val.u8);
        } else {
            return ESP_OK;
        }
    } else if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            next.power = val->val.b ? lgac::Power::On : lgac::Power::Off;
            if (next.power == lgac::Power::On && next.mode == lgac::Mode::Auto) {
                next.mode = lgac::Mode::Cool;
            }
        } else {
            return ESP_OK;
        }
    } else {
        return ESP_OK;
    }

    esp_err_t err = lgac::send(next);
    if (err == ESP_OK) {
        int16_t local_temp = next.temperature_c * 100;
        esp_matter_attr_val_t local_temp_val = esp_matter_int16(local_temp);
        update_attribute(Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id, &local_temp_val);
    }
    return err;
}

static void configure_onboarding_data()
{
    using chip::DeviceLayer::Internal::ESP32Config;

    uint32_t stored_discriminator = 0;
    CHIP_ERROR err = ESP32Config::ReadConfigValue(ESP32Config::kConfigKey_SetupDiscriminator, stored_discriminator);
    if (err != CHIP_NO_ERROR || stored_discriminator != kSetupDiscriminator) {
        err = ESP32Config::WriteConfigValue(ESP32Config::kConfigKey_SetupDiscriminator,
                                            static_cast<uint32_t>(kSetupDiscriminator));
        if (err != CHIP_NO_ERROR) {
            ESP_LOGE(TAG, "Failed to store setup discriminator: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }

    ESP_LOGI(TAG, "Matter setup PIN=20202021 discriminator=%u (0x%03x)",
             kSetupDiscriminator, kSetupDiscriminator);
}

static void configure_wifi_tx_power()
{
    esp_err_t err = esp_wifi_set_max_tx_power(kWifiTxPowerQuarterDbm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set Wi-Fi TX power: %s", esp_err_to_name(err));
        return;
    }

    int8_t configured_power = 0;
    err = esp_wifi_get_max_tx_power(&configured_power);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi TX power limit=%d.%02d dBm",
                 configured_power / 4, (configured_power % 4) * 25);
    } else {
        ESP_LOGW(TAG, "Failed to read Wi-Fi TX power: %s", esp_err_to_name(err));
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed: fail-safe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager &commissioning_mgr =
                chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto timeout = chip::System::Clock::Seconds16(kCommissioningTimeoutSeconds);
            if (!commissioning_mgr.IsCommissioningWindowOpen()) {
                CHIP_ERROR err = commissioning_mgr.OpenBasicCommissioningWindow(
                    timeout, chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Open commissioning window failed: %" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                       uint8_t effect_id, uint8_t effect_variant, void *)
{
    ESP_LOGI(TAG, "Identify endpoint=%u type=%u effect=%u variant=%u",
             endpoint_id, type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                                         uint32_t cluster_id, uint32_t attribute_id,
                                         esp_matter_attr_val_t *val, void *)
{
    if (type != PRE_UPDATE || endpoint_id != s_thermostat_endpoint_id) {
        return ESP_OK;
    }
    return sync_ir_from_matter(cluster_id, attribute_id, val);
}

} // namespace

extern "C" void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    configure_onboarding_data();
    ESP_ERROR_CHECK(lgac::init());

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    abort_on_failure(node != nullptr, "Failed to create Matter node");

    endpoint::thermostat::config_t thermostat_config;
    thermostat_config.thermostat.local_temperature = CONFIG_LG_IR_DEFAULT_TEMP_C * 100;
    thermostat_config.thermostat.system_mode = 0;
    thermostat_config.thermostat.control_sequence_of_operation = 4; // Cooling and heating.
    thermostat_config.thermostat.features.cooling.occupied_cooling_setpoint = CONFIG_LG_IR_DEFAULT_TEMP_C * 100;
    thermostat_config.thermostat.features.heating.occupied_heating_setpoint = CONFIG_LG_IR_DEFAULT_TEMP_C * 100;
    thermostat_config.thermostat.feature_flags = cluster::thermostat::feature::cooling::get_id() |
                                                 cluster::thermostat::feature::heating::get_id();

    endpoint_t *endpoint = endpoint::thermostat::create(node, &thermostat_config, ENDPOINT_FLAG_NONE, nullptr);
    abort_on_failure(endpoint != nullptr, "Failed to create thermostat endpoint");
    s_thermostat_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Thermostat endpoint id=%u", s_thermostat_endpoint_id);

    cluster::on_off::config_t onoff_config;
    onoff_config.on_off = false;
    cluster_t *onoff_cluster = cluster::on_off::create(endpoint, &onoff_config, CLUSTER_FLAG_SERVER);
    abort_on_failure(onoff_cluster != nullptr, "Failed to create on/off cluster");

    cluster::fan_control::config_t fan_config;
    fan_config.fan_mode = 5;
    fan_config.fan_mode_sequence = 2;
    cluster_t *fan_cluster = cluster::fan_control::create(endpoint, &fan_config, CLUSTER_FLAG_SERVER);
    abort_on_failure(fan_cluster != nullptr, "Failed to create fan control cluster");

    err = esp_matter::start(app_event_cb);
    abort_on_failure(err == ESP_OK, "Failed to start Matter");
    configure_wifi_tx_power();
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}

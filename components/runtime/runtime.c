#include "runtime.h"

#include <stdio.h>
#include <string.h>

#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "hal.h"
#include "lua.h"
#include "lua_port.h"
#include "sdmmc_cmd.h"

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

static const char *TAG = "runtime";

static sdmmc_card_t *g_card;

static esp_err_t mount_sdcard(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount("/sd", &host, &slot_config, &mount_config, &g_card);
    if (ret != ESP_OK) {
        spi_bus_free(host.slot);
        return ret;
    }

    return ESP_OK;
}

static void push_event(lua_runtime_t *rt, const char *type)
{
    lua_State *L = rt->L;
    lua_newtable(L);
    lua_pushstring(L, type);
    lua_setfield(L, -2, "type");
}

void runtime_start(void)
{
    hal_init();

    esp_err_t ret = mount_sdcard();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        return;
    }

    lua_runtime_t rt = {0};
    if (!lua_port_init(&rt)) {
        ESP_LOGE(TAG, "Lua init failed");
        return;
    }

    const char *app_path = "/sd/apps/main.lua";
    if (!lua_port_execute_file(&rt, app_path)) {
        ESP_LOGE(TAG, "failed to execute %s", app_path);
        lua_port_deinit(&rt);
        return;
    }

    if (!lua_port_call_global(&rt, "init", 0, 0)) {
        ESP_LOGW(TAG, "init() not found or failed");
    }

    uint32_t tick = 0;
    while (1) {
        if (!lua_port_call_global(&rt, "update", 0, 0)) {
            ESP_LOGW(TAG, "update() missing or failed");
        }

        tick += 50;
        if (tick >= 1000) {
            push_event(&rt, "button_press");
            (void)lua_port_call_global(&rt, "on_event", 1, 0);
            tick = 0;
        }

        hal_delay_ms(50);
    }
}

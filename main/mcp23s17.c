#include "mcp23s17.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp23s17";

#define PIN_MISO 12
#define PIN_MOSI 13
#define PIN_SCK  14
#define PIN_CS   15

#define MCP_WRITE 0x40
#define MCP_READ  0x41

#define REG_IODIRA 0x00
#define REG_GPPUA  0x0C
#define REG_GPIOA  0x12
#define REG_OLATA  0x14
#define REG_IOCON  0x0A

/* 24 bits at 5 MHz is ~5 us; 80 ms bounds a wedged controller. */
#define MCP_SPI_WAIT pdMS_TO_TICKS(80)

static spi_device_handle_t s_spi;
static bool s_ok;
static uint8_t s_present;
static char s_fault[128];
static char s_spi_error[48];
static uint16_t s_iodir[MCP23S17_CHIP_COUNT];
static uint16_t s_gppu[MCP23S17_CHIP_COUNT];
static uint16_t s_olat[MCP23S17_CHIP_COUNT];

static uint8_t opcode_write(int chip)
{
    return (uint8_t)(MCP_WRITE | ((chip & 0x07) << 1));
}

static uint8_t opcode_read(int chip)
{
    return (uint8_t)(MCP_READ | ((chip & 0x07) << 1));
}

static void refresh_fault(void)
{
    if (s_spi_error[0]) {
        snprintf(s_fault, sizeof(s_fault),
                 "Hardware: %s — MCP23S17 GPIO expanders unavailable.", s_spi_error);
        return;
    }
    if (s_ok) {
        s_fault[0] = '\0';
        return;
    }
    if (s_present == 0) {
        snprintf(s_fault, sizeof(s_fault),
                 "Hardware: MCP23S17 GPIO expanders not found (chips 0–1 missing). Cabinet I/O disabled.");
    } else {
        snprintf(s_fault, sizeof(s_fault),
                 "Hardware: MCP23S17 chip %d missing. Cabinet I/O disabled.",
                 (s_present & 0x01) ? 1 : 0);
    }
}

static esp_err_t mcp_xfer(const uint8_t *tx, uint8_t *rx)
{
    spi_transaction_t t = {0};
    if (!s_spi) {
        return ESP_ERR_INVALID_STATE;
    }
    t.length = 24;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    /* IDF 6 rejects polling_start with a finite timeout; polling_transmit
     * cannot time out. Interrupt transactions give a bounded wait. */
    spi_transaction_t *done = NULL;
    esp_err_t err = spi_device_queue_trans(s_spi, &t, MCP_SPI_WAIT);
    if (err != ESP_OK) {
        return err;
    }
    err = spi_device_get_trans_result(s_spi, &done, MCP_SPI_WAIT);
    if (err != ESP_OK) {
        return err;
    }
    return (done == &t) ? ESP_OK : ESP_FAIL;
}

static esp_err_t mcp_write8(int chip, uint8_t reg, uint8_t val)
{
    uint8_t tx[3] = { opcode_write(chip), reg, val };
    return mcp_xfer(tx, NULL);
}

static esp_err_t mcp_read8(int chip, uint8_t reg, uint8_t *out)
{
    uint8_t tx[3] = { opcode_read(chip), reg, 0 };
    uint8_t rx[3] = {0};
    esp_err_t err = mcp_xfer(tx, rx);
    if (err == ESP_OK && out) {
        *out = rx[2];
    }
    return err;
}

static bool probe_chip(int chip)
{
    uint8_t a = 0xFF;
    uint8_t b = 0xFF;
    if (mcp_write8(chip, REG_IODIRA, 0xA5) != ESP_OK) {
        return false;
    }
    if (mcp_read8(chip, REG_IODIRA, &a) != ESP_OK || a != 0xA5) {
        return false;
    }
    if (mcp_write8(chip, REG_IODIRA, 0x5A) != ESP_OK) {
        return false;
    }
    if (mcp_read8(chip, REG_IODIRA, &b) != ESP_OK || b != 0x5A) {
        return false;
    }
    return true;
}

static esp_err_t mcp_write16(int chip, uint8_t reg_a, uint16_t val)
{
    esp_err_t err = mcp_write8(chip, reg_a, (uint8_t)(val & 0xFF));
    if (err != ESP_OK) {
        return err;
    }
    return mcp_write8(chip, (uint8_t)(reg_a + 1), (uint8_t)(val >> 8));
}

static esp_err_t flush_chip(int chip)
{
    esp_err_t err = mcp_write16(chip, REG_IODIRA, s_iodir[chip]);
    if (err != ESP_OK) {
        return err;
    }
    err = mcp_write16(chip, REG_OLATA, s_olat[chip]);
    if (err != ESP_OK) {
        return err;
    }
    return mcp_write16(chip, REG_GPPUA, s_gppu[chip]);
}

static bool split_pin(int logical, int *chip, int *port)
{
    if (logical < 0 || logical >= MCP23S17_LOGICAL_COUNT) {
        return false;
    }
    *chip = logical / MCP23S17_PORT_COUNT;
    *port = logical % MCP23S17_PORT_COUNT;
    return true;
}

esp_err_t mcp23s17_init(void)
{
    spi_bus_config_t bus = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 5 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .flags = 0,
    };

    s_ok = false;
    s_present = 0;
    gpio_reset_pin(PIN_CS);
    gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS, 1);

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        snprintf(s_spi_error, sizeof(s_spi_error), "SPI bus init failed (%s)",
                 esp_err_to_name(err));
        refresh_fault();
        return ESP_OK;
    }

    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        snprintf(s_spi_error, sizeof(s_spi_error), "SPI device add failed (%s)",
                 esp_err_to_name(err));
        s_spi = NULL;
        refresh_fault();
        return ESP_OK;
    }

    (void)mcp23s17_probe();
    if (s_ok) {
        ESP_LOGI(TAG, "MCP23S17 addr 0 and 1 ready on HSPI");
    } else {
        ESP_LOGW(TAG, "%s", s_fault);
    }
    return ESP_OK;
}

bool mcp23s17_probe(void)
{
    uint8_t found = 0;
    int chip;

    if (!s_spi) {
        s_ok = false;
        s_present = 0;
        refresh_fault();
        return false;
    }
    for (chip = 0; chip < MCP23S17_CHIP_COUNT; ++chip) {
        (void)mcp_write8(chip, REG_IOCON, 0x08);
        if (probe_chip(chip)) {
            found |= (uint8_t)(1u << chip);
            s_iodir[chip] = 0xFFFF;
            s_gppu[chip] = 0xFFFF;
            s_olat[chip] = 0x0000;
            (void)flush_chip(chip);
        }
    }
    s_present = found;
    s_ok = (found == 0x03);
    refresh_fault();
    return s_ok;
}

bool mcp23s17_ok(void)
{
    return s_ok;
}

void mcp23s17_get_fault(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (s_ok) {
        out[0] = '\0';
        return;
    }
    strncpy(out, s_fault, out_size - 1);
    out[out_size - 1] = '\0';
}

void mcp23s17_pin_output(int logical)
{
    int chip;
    int port;
    uint16_t mask;

    if (!s_ok || !split_pin(logical, &chip, &port)) {
        return;
    }
    mask = (uint16_t)(1u << port);
    s_iodir[chip] &= (uint16_t)~mask;
    (void)mcp_write16(chip, REG_IODIRA, s_iodir[chip]);
}

void mcp23s17_digital_write(int logical, int level)
{
    int chip;
    int port;
    uint16_t mask;

    if (!s_ok || !split_pin(logical, &chip, &port)) {
        return;
    }
    mask = (uint16_t)(1u << port);
    if (level) {
        s_olat[chip] |= mask;
    } else {
        s_olat[chip] &= (uint16_t)~mask;
    }
    (void)mcp_write16(chip, REG_OLATA, s_olat[chip]);
}

void mcp23s17_pin_input_pullup(int logical)
{
    int chip;
    int port;
    uint16_t mask;

    if (!s_ok || !split_pin(logical, &chip, &port)) {
        return;
    }
    mask = (uint16_t)(1u << port);
    s_iodir[chip] |= mask;
    s_gppu[chip] |= mask;
    (void)mcp_write16(chip, REG_IODIRA, s_iodir[chip]);
    (void)mcp_write16(chip, REG_GPPUA, s_gppu[chip]);
}

uint16_t mcp23s17_read_gpio(int chip)
{
    uint8_t a = 0xFF;
    uint8_t b = 0xFF;

    if (!s_ok || chip < 0 || chip >= MCP23S17_CHIP_COUNT) {
        return 0xFFFF;
    }
    if (mcp_read8(chip, REG_GPIOA, &a) != ESP_OK) {
        s_ok = false;
        return 0xFFFF;
    }
    if (mcp_read8(chip, REG_GPIOA + 1, &b) != ESP_OK) {
        s_ok = false;
        return 0xFFFF;
    }
    return (uint16_t)a | ((uint16_t)b << 8);
}

int mcp23s17_digital_read(int logical)
{
    int chip;
    int port;
    uint16_t gpio;

    if (!split_pin(logical, &chip, &port)) {
        return 1;
    }
    gpio = mcp23s17_read_gpio(chip);
    return (gpio & (1u << port)) ? 1 : 0;
}

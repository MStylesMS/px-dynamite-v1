#include "mcp23s17.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

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

static spi_device_handle_t s_spi;
static bool s_ok;
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

static esp_err_t mcp_write8(int chip, uint8_t reg, uint8_t val)
{
    spi_transaction_t t = {0};
    uint8_t tx[3] = { opcode_write(chip), reg, val };

    t.length = 24;
    t.tx_buffer = tx;
    t.rx_buffer = NULL;
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t mcp_read8(int chip, uint8_t reg, uint8_t *out)
{
    spi_transaction_t t = {0};
    uint8_t tx[3] = { opcode_read(chip), reg, 0 };
    uint8_t rx[3] = {0};

    t.length = 24;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    if (err == ESP_OK && out) {
        *out = rx[2];
    }
    return err;
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
    int chip;

    s_ok = false;
    gpio_reset_pin(PIN_CS);
    gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS, 1);

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        return err;
    }

    for (chip = 0; chip < MCP23S17_CHIP_COUNT; ++chip) {
        uint8_t iodir = 0;
        /* BANK=0, HAEN on so A0/A1/A2 select the chip. */
        (void)mcp_write8(chip, REG_IOCON, 0x08);
        s_iodir[chip] = 0xFFFF;
        s_gppu[chip] = 0xFFFF;
        s_olat[chip] = 0x0000;
        err = flush_chip(chip);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "MCP addr %d register init failed: %s", chip, esp_err_to_name(err));
            return err;
        }
        err = mcp_read8(chip, REG_IODIRA, &iodir);
        if (err != ESP_OK || iodir != 0xFF) {
            ESP_LOGE(TAG, "MCP addr %d probe failed (IODIRA=0x%02X err=%s)",
                     chip, iodir, esp_err_to_name(err));
            return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
        }
    }

    s_ok = true;
    ESP_LOGI(TAG, "MCP23S17 addr 0 and 1 ready on HSPI");
    return ESP_OK;
}

bool mcp23s17_ok(void)
{
    return s_ok;
}

void mcp23s17_pin_output(int logical)
{
    int chip;
    int port;
    uint16_t mask;

    if (!split_pin(logical, &chip, &port)) {
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

    if (!split_pin(logical, &chip, &port)) {
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

    if (!split_pin(logical, &chip, &port)) {
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

    if (chip < 0 || chip >= MCP23S17_CHIP_COUNT) {
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

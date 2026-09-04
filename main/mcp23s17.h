#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MCP23S17_CHIP_COUNT 2
#define MCP23S17_PORT_COUNT 16
#define MCP23S17_LOGICAL_COUNT 32

esp_err_t mcp23s17_init(void);
bool mcp23s17_ok(void);

void mcp23s17_pin_output(int logical);
void mcp23s17_digital_write(int logical, int level);
void mcp23s17_pin_input_pullup(int logical);
int mcp23s17_digital_read(int logical);
uint16_t mcp23s17_read_gpio(int chip);

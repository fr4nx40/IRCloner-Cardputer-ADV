// config.h
#pragma once

#include <cstdint>

// App version
constexpr const char *APP_VERSION = "v1.0.1";

// Storage paths
constexpr const char *IR_DIR = "/ircloner";
constexpr const char *CONFIG_PATH = "/IRCloner/config.config";

// Default IR pins
constexpr uint8_t DEFAULT_IR_TX_PIN = 44;
constexpr uint8_t DEFAULT_IR_RX_PIN = 1;

// Global limits
constexpr uint8_t MAX_GPIO = 48;         
constexpr uint8_t MAX_SIGNALS = 64;
constexpr uint8_t MAX_FILES = 32;    

//Input keys
constexpr char BACK_KEY = '`';
constexpr char DELETE_KEY = 'd';


// UI
uint8_t browserTop = 0;
constexpr uint8_t GRID_COLUMNS = 4;
constexpr uint8_t GRID_ROWS = 3;
constexpr uint8_t GRID_PAGE_SIZE = GRID_COLUMNS * GRID_ROWS;

// UI colours
constexpr uint16_t UI_BG = 0x0841;
constexpr uint16_t UI_PINK = 0xF1B5;
constexpr uint16_t UI_CYAN = 0x07FF;
constexpr uint16_t UI_YELLOW = 0xFFE0;
constexpr uint16_t UI_WHITE = 0xFFFF;
constexpr uint16_t UI_DIM = 0x4A49;
constexpr uint16_t UI_RED = 0xF800;
constexpr uint16_t UI_GREEN = 0x07E0;

// Timing / behaviour
constexpr uint32_t CAPTURE_DEBOUNCE_MS = 450;
constexpr uint32_t RAW_SEND_SPLIT_US = 60000;

// Brightness / dim
constexpr uint8_t MIN_BRIGHTNESS = 10;
constexpr uint8_t MAX_BRIGHTNESS = 255;
constexpr uint8_t BRIGHTNESS_STEP = 15;

constexpr uint16_t  DIM_CHOICES[] = { 0, 15, 30, 60, 120, 300 };
constexpr uint8_t   DIM_CHOICE_COUNT = sizeof(DIM_CHOICES) / sizeof(DIM_CHOICES[0]);
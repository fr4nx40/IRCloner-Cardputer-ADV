
![IR Cloner Header Banner](media/images/splash.jpg)

# IR Cloner for M5Stack Cardputer ADV

An infrared remote capture, storage, and replay application for the M5Stack Cardputer ADV.

IR Cloner can capture supported decoded infrared commands, save remotes as `.ir` files, load and replay saved commands, and work with both the SD card and LittleFS storage.

## Features

- Capture supported decoded IR protocols
- Replay captured IR commands
- Save and load remotes in Flipper Zero `.ir` format
- Support for parsed and raw IR signals
- SD card and LittleFS storage support
- Remote/button naming editor
- Remote browser and button-grid interface
- Configurable IR transmit and receive GPIO pins
- Saved display brightness and timeout settings
- IR transmitter/receiver self-test screen
- Continuous or counted IR replay mode

## Hardware

This project targets:

- [M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv)
- IR receiver module connected to the configured RX GPIO
- IR LED/transmitter circuit connected to the configured TX GPIO
- Also compatible with the [M5Stack IR Unit](https://docs.m5stack.com/en/unit/ir)
- Optional microSD card for SD storage

### Default IR pins

| Function | Default GPIO |
|---|---:|
| IR transmit | GPIO 44 |
| IR receive | GPIO 1 |

You can change the TX and RX pins from the app's Settings screen.

## Screenshots

![Splash screen](media/images/plash%202.jpg)

![Menu screen](media/images/Frame%204.jpg)

![Remote screen](media/images/Frame%205.jpg)

![Settings screen](media/images/Frame%203.jpg)


## Wiring

### IR receiver

Typical 3-pin IR receiver modules have:

| Receiver pin | Connect to |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| OUT | Configured IR RX GPIO |


### IR transmitter

Typical 3-pin IR transmitter modules have:

| Transmitter connection | Connect to |
|---|---|
| GPIO input / transistor base resistor | Configured IR TX GPIO |
| Transistor emitter/source | GND |
| IR LED supply path | Suitable current-limited LED supply |
| Cardputer ground | Shared GND with transmitter circuit |

> Do not connect a high-power IR LED directly to a GPIO without checking current limits and using an appropriate resistor/driver circuit.

## Firmware binary

Release binaries are available from this repository's GitHub **Releases** page.

## Storage

IR Cloner can use:

- **LittleFS**: onboard flash storage
- **SD Card**: removable storage, if available

The application creates its IR directory automatically:

```text
/ircloner
```

Saved remote files use the `.ir` extension and are compatible with Flipper Zero IR code files.

Example filename:

```text
/ircloner/TV_REMOTE.ir
```

## Controls

| Key | Action |
|---|---|
| `;` | Up |
| `.` | Down |
| `,` | Left |
| `/` | Right |
| Enter | Select / Confirm |
| `` ` `` | Back |
| `S` | Save |
| `R` | Replay |
| `D` | Delete |

## Known limitations

- Reliable raw-signal replay depends on the IR transmitter circuit, the carrier frequency, timing accuracy, and the original remote protocol.
- IR receiver modules are generally designed for carrier frequencies around 38 kHz, though the actual supported range depends on your specific receiver.
- Long raw captures use RAM; very large remote files may be limited by available memory.

## Contributing

Issues, bug reports, hardware wiring improvements, and protocol compatibility reports are welcome.

When reporting an issue, include:

- Cardputer model
- IR receiver and transmitter hardware
- TX/RX GPIO settings
- Serial Monitor output
- Steps to reproduce the problem


## Credits

[Bruce](https://bruce.computer/)

[UniGeek](https://unigeek.xid.run/)

[Juj](https://github.com/juj) for [image to rgb565 generation](https://github.com/juj/ST7735R/blob/master/image_to_rgb565.py)

Claude and Perplextity AI Tools

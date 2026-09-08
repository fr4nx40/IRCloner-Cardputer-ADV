
![IR Cloner Header Banner](media/images/splash.jpg)

# IR Cloner for M5Stack Cardputer ADV

An ESP32-S3 infrared remote capture, storage, and replay application for the M5Stack Cardputer ADV.

IR Cloner can capture supported decoded infrared commands, save remotes as `.ir` files, load and replay saved commands, and work with both the SD card and LittleFS storage.

> ⚠️ Use this project only with equipment you own or are authorised to control.

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

- M5Stack Cardputer ADV
- ESP32-S3-based Cardputer hardware
- IR receiver module connected to the configured RX GPIO
- IR LED/transmitter circuit connected to the configured TX GPIO
- Optional microSD card for SD storage

### Default IR pins

| Function | Default GPIO |
|---|---:|
| IR transmit | GPIO 44 |
| IR receive | GPIO 1 |

You can change the TX and RX pins from the app's Settings screen.

## Screenshots

Place your screenshots in `docs/images/`.

![Home screen](docs/images/home-screen.png)

![Remote grid](docs/images/remote-grid.png)

![Settings screen](docs/images/settings-screen.png)

## Wiring

### IR receiver

Typical 3-pin IR receiver modules have:

| Receiver pin | Connect to |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| OUT | Configured IR RX GPIO |



### IR transmitter

For reliable range, drive the IR LED through a transistor or MOSFET stage rather than directly from an ESP32 GPIO.

| Transmitter connection | Connect to |
|---|---|
| GPIO input / transistor base resistor | Configured IR TX GPIO |
| Transistor emitter/source | GND |
| IR LED supply path | Suitable current-limited LED supply |
| Cardputer ground | Shared GND with transmitter circuit |

> Do not connect a high-power IR LED directly to a GPIO without checking current limits and using an appropriate resistor/driver circuit.

## Building from source

### Requirements

- [Visual Studio Code](https://code.visualstudio.com/)
- [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
- USB data cable
- M5Stack Cardputer ADV

### Build

Open the project folder containing `platformio.ini`, then run:

```bash
pio run
```

### Upload

Connect the Cardputer ADV by USB, then run:

```bash
pio run --target upload
```

Or choose **Upload** from PlatformIO Project Tasks.

## Firmware binary

After a successful build, PlatformIO creates the firmware binary here:

```text
.pio/build/<environment-name>/firmware.bin
```

For example, if your `platformio.ini` contains:

```ini
[env:m5stack-stamps3]
```

your binary is here:

```text
.pio/build/m5stack-stamps3/firmware.bin
```

Release binaries are available from this repository's GitHub **Releases** page.

## Storage

IR Cloner can use:

- **LittleFS**: onboard flash storage
- **SD Card**: removable storage, if available

The application creates its IR directory automatically:

```text
/ircloner
```

Saved remote files use the `.ir` extension and compatible with Flipper Zero IR code files.

Example filename:

```text
/ircloner/TV_REMOTE.ir
```

## Project structure

```text
.
├── include/
│   ├── core/
│   ├── ir/
│   ├── remote/
│   └── splash/
├── lib/
├── src/
│   ├── core/
│   ├── ir/
│   ├── remote/
│   ├── splash/
│   └── main.cpp
├── docs/
│   └── images/
├── platformio.ini
├── LICENSE
└── README.md
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
- Some remotes use rolling codes, encrypted commands, or proprietary protocols and cannot be cloned reliably with a simple capture/replay workflow.
- IR receiver modules are generally designed for carrier frequencies around 38 kHz, though the actual supported range depends on your specific receiver.
- Long raw captures use RAM; very large remote files may be limited by available memory.

## Contributing

Issues, bug reports, hardware wiring improvements, and protocol compatibility reports are welcome.

When reporting an issue, include:

- Cardputer model
- PlatformIO environment name
- IR receiver and transmitter hardware
- TX/RX GPIO settings
- Serial Monitor output
- Steps to reproduce the problem


## Credits

Bruce https://bruce.computer/
UniGeek https://unigeek.xid.run/
Juj https://github.com/juj for image rgb565 generation https://github.com/juj/ST7735R/blob/master/image_to_rgb565.py
Claude and Perplextity AI Tools

## License

Choose and add a licence before publishing. The MIT License is a common choice for hobby firmware projects.
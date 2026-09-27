# ESP8266 Matrix Clock

A WiFi LED-matrix clock for an **ESP8266 (NodeMCU / Wemos D1 mini)** and a **4-in-1 MAX7219 8x32 LED module**.
The clock sets itself from the internet, and you configure everything from your phone. There's nothing to
edit in the code before you flash it.

- Internet time (NTP), India (IST) by default, any time zone selectable
- 12 or 24 hour, blinking colon, seconds bar
- Digit change animations: Roll down, Roll up, Dissolve, Slide, Flip, Drop & bounce, Random
- Scrolling date, fixed welcome message at power-on
- Animations: Sparkle, Wipe, Rain, Boxes, Pac-Man (every hour and/or at power-on)
- Brightness slider and **night mode** (dim or switch off at night)
- **First-time WiFi setup from a phone:** the clock opens its own hotspot with a setup page
- Settings web page at the clock's IP address or `http://matrixclock.local`

## Hardware

| MAX7219 module | ESP8266 (NodeMCU / D1 mini) | GPIO |
|---|---|---|
| VCC | 5V (VU / VIN) | - |
| GND | GND | - |
| DIN | D7 | GPIO13 (SPI MOSI) |
| CS  | D6 | GPIO12 |
| CLK | D5 | GPIO14 (SPI SCK) |

Power it from a decent **5 V / 1 A** USB supply. At full brightness with many LEDs lit, a weak supply or
thin cable can brown out the ESP8266 and make it restart.

## Flashing

With [PlatformIO](https://platformio.org/):

```bash
pio run -e nodemcuv2 -t upload      # NodeMCU
pio run -e d1_mini -t upload        # Wemos D1 mini
```

The only dependency, [MD_MAX72XX](https://github.com/MajicDesigns/MD_MAX72XX), is downloaded automatically.
If the text looks scrambled, mirrored or upside down, change `MATRIX_HW`, `FLIP_HORIZONTAL` or
`FLIP_VERTICAL` in [`src/config.h`](src/config.h).

## First-time setup (for whoever receives the clock)

1. Plug the clock in. It plays a short animation, shows its welcome text, and then scrolls
   **"WiFi setup: join MatrixClock-XXXX then open 192.168.4.1"**.
2. On your phone, open WiFi settings and join the network **MatrixClock-XXXX** (no password).
3. A setup page should pop up by itself. If it doesn't, open **http://192.168.4.1** in the browser.
4. Tap your home WiFi network, type its password and press **Save & connect**.
5. The clock restarts, joins your WiFi and **scrolls its IP address** (for example `IP 192.168.1.23`).
   Then it shows the time.
6. Reconnect your phone to your home WiFi and open that IP address in the browser (or try
   `http://matrixclock.local`) to change the settings.

If the password was wrong, the setup hotspot comes back. Join it again and retry.
If the home WiFi is only temporarily down (for example the router is still starting after a power cut),
the clock keeps retrying it every 2 minutes while in setup mode.

## Settings page

| Section | Options |
|---|---|
| Brightness | Brightness slider (live preview), night mode on/off, night hours, night brightness or display off |
| Clock | 12/24 hour, time zone, blinking colon, seconds bar, leading zero, scrolling date |
| Animations | Digit change (roll down / roll up / dissolve / slide / flip / drop & bounce / random / instant, previewed on the clock when picked), hourly animation, power-on animation, try-out buttons |
| WiFi & system | Network and signal, IP, uptime, change WiFi, show IP, re-sync time, restart, factory reset |

Settings are stored in flash and survive power cuts.

The welcome message is fixed in the firmware (`WELCOME_TEXT` in [`src/config.h`](src/config.h)) and can't be changed from the web page.

## FLASH button (NodeMCU)

- **Short press:** scroll the IP address
- **Hold 5 seconds:** forget the WiFi network and go back to setup mode

The D1 mini has no FLASH button. Use **Change WiFi** or **Factory reset** on the settings page instead.
If the saved WiFi can't be reached at power-up, setup mode opens by itself anyway.

## Project layout

```
src/
  main.cpp       start-up, WiFi / setup mode, clock loop, button
  display.*      frame buffer, text, clock face (5x7 digits), animations
  web.*          settings page, WiFi setup page, captive portal, JSON API
  web_pages.h    HTML / CSS
  settings.*     settings stored in flash
  config.h       pins, display type, defaults
```

## License

MIT, see [LICENSE](LICENSE).

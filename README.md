# M5Mp3 for M5Cardputer

A Winamp-style MP3 player for the M5Cardputer, adapted from the original M5Mp3 project.

---

## Credits

This project is adapted from [M5Mp3 by VolosR](https://github.com/VolosR/M5Mp3). Special thanks to VolosR for the original Winamp-style interface and audio playback implementation, and to [Andy (AndyAiCardputer)](https://github.com/AndyAiCardputer/mp3-player-winamp-cardputer-adv) for the groundwork on this fork.

---

## Key Changes from Original

- Replaced `ESP32-audioI2S` with `ESP8266Audio`
- Uses `M5Cardputer.Speaker` (ES8311) instead of I2S
- Custom `AudioOutput` class for M5Cardputer
- Removed `ESP32Time` dependency (uses `millis()`)
- Added ES8311 audio codec support

---

## Required Libraries

- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio)
- M5Cardputer (included with M5Stack board support)

---

## Features

- Winamp-style UI with FFT visualization
- MP3 playback from microSD card
- Volume, brightness, and track controls
- Playlist navigation and random track selection
- Real-time audio spectrum analyzer

---

## Hardware Setup

1. Connect a microSD card with MP3 files in a `/mp3` folder.
2. Ensure the M5Cardputer is powered and the SD card is inserted.

---

## Usage

- **Play/Pause:** Press `A` or `ENTER`
- **Next Track:** Press `N` or `.`(Arrow Down)
- **Previous Track:** Press `P` or `;`(Arrow Up)
- **Random Track:** Press `B`
- **Volume:** Press `V` or adjust slider
- **Brightness:** Press `L` or adjust slider
- **Seek:** Use the seek slider or `-`/`=` keys

---

## File Structure

- Place MP3 files in the `/mp3` folder on the SD card.
- The player will automatically scan and list available tracks.

---

## License

Same as the original M5Mp3 project.

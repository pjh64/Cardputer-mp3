# M5Mp3 for M5Cardputer

A Winamp-style MP3 player for the M5Cardputer, adapted from the original M5Mp3 project.

---

## Credits

This project is adapted from [M5Mp3 by VolosR](https://github.com/VolosR/M5Mp3). Special thanks to VolosR for the original Winamp-style interface and audio playback implementation, and to [Andy (AndyAiCardputer)](https://github.com/AndyAiCardputer/mp3-player-winamp-cardputer-adv) for the groundwork on this fork.

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
- Fast forward / Rewind
---

## Hardware Setup

1. Connect a microSD card with MP3 files in a `/mp3` folder.
2. Insert the SD and power on (May take a few seconds depending on the numer of songs in the mp3 directory).

---

## Usage

- Play/Pause: Press `A` or `ENTER`
- Next Track: Press `N` or `.`(Arrow Down)
- Previous Track: Press `P` or `;`(Arrow Up)
- Random Track: Press `B`
- Volume: Press `V` or adjust slider
- Brightness: Press `L` or adjust slider
- Set cursor: Left/Right Arrow Key, up/down to select slider / button
- Seek-Slider:** Use `-` to rewind `+` to Fast forward


---

## File Structure

- Place MP3 files in the `/mp3` folder on the SD card.
- The player will automatically scan and list available tracks.

---

## License

Same as the original M5Mp3 project.

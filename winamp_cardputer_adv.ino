 M5Mp3 Winamp-style MP3 Player for M5Stack Cardputer-Adv
 * 
 * ============================================================================
 * CREDITS
 * ============================================================================
 * 
 * This project is adapted from the original M5Mp3 by VolosR:
 * - Original Project: https://github.com/VolosR/M5Mp3
 * - Original Author: VolosR
 * - License: Same as original M5Mp3 project
 * 
 * Many thanks to VolosR for creating the original Winamp-style interface
 * and audio playback implementation!
 * 
 * ============================================================================
 * CHANGES FROM ORIGINAL
 * ============================================================================
 * 
 * - Replaced ESP32-audioI2S with ESP8266Audio
 * - Uses M5Cardputer.Speaker (ES8311) instead of I2S
 * - Custom AudioOutput class for M5Cardputer
 * - Removed ESP32Time dependency (uses millis() instead)
 * - Added ES8311 audio codec support
 * 
 * ============================================================================
 * LIBRARIES REQUIRED
 * ============================================================================
 * 
 * - ESP8266Audio (https://github.com/earlephilhower/ESP8266Audio)
 * - M5Cardputer (included with M5Stack board support)
 * Thanks to Andy (AndyAiCardputer) who created the groundwork for this Fork.
 */
     
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <M5Cardputer.h>
#include <arduinoFFT.h>
#include <memory>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ESP8266Audio libraries
#include <AudioOutput.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>

M5Canvas sprite(&M5Cardputer.Display);
M5Canvas spr(&M5Cardputer.Display);

// microSD card pins
#define SD_SCK 40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS 12

// FFT parameters
#define SAMPLING_FREQUENCY 44100
#define FFT_SIZE 128
#define FFT_AVERAGING 128
#define FFT_WINDOW_X 170
#define FFT_WINDOW_Y 40
#define FFT_WINDOW_WIDTH 65
#define FFT_WINDOW_HEIGHT 15
#define FFT_BAR_WIDTH 1
#define FFT_BAR_SPACING 2
#define SCROLL_SPEED 1

ArduinoFFT<double> FFT = ArduinoFFT<double>();
double vReal[FFT_SIZE];
double vImag[FFT_SIZE];
double fftOutput[FFT_SIZE / 2];
unsigned long lastFFTTime = 0;

// --- Thread Safety ---
SemaphoreHandle_t stateMutex = xSemaphoreCreateMutex();

// --- State Encapsulation ---
class PlayerState {
public:
    int volume = 2;
    int brightnessIndex = 0;
    int currentTrack = 0;
    int seekValue = 0;
    unsigned long seekPosition = 0;
    unsigned long trackDuration = 100000; // Default, updated from ID3
    bool isPlaying = true;
    bool isStopped = false;
    bool nextTrackRequested = false;
    bool volumeUpdated = false;
    int textSize = 1;
    int graphSpeed = 0;
    bool cursorOnRight = false;
    enum Control { SEEK, VOLUME, BRIGHTNESS, BUTTON_A, BUTTON_P, BUTTON_N, BUTTON_B };
    Control activeControl = SEEK;
};

PlayerState playerState;

// --- UI Component Classes ---
struct UIComponent {
    virtual ~UIComponent() = default;
    virtual void draw(M5Canvas &sprite) = 0;
    virtual void update() {}
    virtual bool contains(int px, int py) { return false; }
    virtual void setActive(bool active) {}
    virtual void increment() {}
    virtual void decrement() {}
    virtual void setValue(int newValue) {}
    virtual int getX() const { return 0; }
    virtual int getY() const { return 0; }
    virtual int getWidth() const { return 0; }
    virtual int getHeight() const { return 0; }
};

class MyFFTWindow : public UIComponent {
public:
    MyFFTWindow(int x, int y, int width, int height) : _x(x), _y(y), _width(width), _height(height) {}

    void draw(M5Canvas &sprite) override {
        sprite.fillRect(_x, _y, _width, _height, BLACK);
        int maxBars = _width / (FFT_BAR_WIDTH + FFT_BAR_SPACING);
        int barsToDraw = std::min(maxBars, FFT_SIZE / 2);
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        for (int i = 0; i < barsToDraw; i++) {
            double scaledValue = log10(1 + fftOutput[i]) * 10;
            int barHeight = constrain(scaledValue, 0, _height);
            int barX = _x + (i * (FFT_BAR_WIDTH + FFT_BAR_SPACING));
            int barY = _y + _height - barHeight;
            uint16_t barColor = (i < barsToDraw / 3) ? RED : (i < 2 * barsToDraw / 3) ? YELLOW : GREEN;
            sprite.fillRect(barX, barY, FFT_BAR_WIDTH, barHeight, barColor);
        }
        xSemaphoreGive(stateMutex);
    }

    int getX() const override { return _x; }
    int getY() const override { return _y; }
    int getWidth() const override { return _width; }
    int getHeight() const override { return _height; }

private:
    int _x, _y, _width, _height;
};

class ScrollingText : public UIComponent {
public:
    ScrollingText(int x, int y, int width, int height) : _x(x), _y(y), _width(width), _height(height), scrollPos(0), scrollDirection(true) {}

    void draw(M5Canvas &sprite) override {
        sprite.fillRect(_x, _y, _width, _height, BLACK);
    }

    void draw(M5Canvas &sprite, const String &text) {
        sprite.fillRect(_x, _y, _width, _height, BLACK);
        int maxVisibleChars = _width / 6;
        int textLength = text.length();
        int startPos = scrollPos;
        int endPos = std::min(startPos + maxVisibleChars, textLength);
        String visibleText = text.substring(startPos, endPos);
        sprite.setTextColor(GREEN, BLACK);
        sprite.setTextSize(1);
        sprite.drawString(visibleText, _x + 30, _y);

        if (scrollDirection) {
            scrollPos++;
            if (scrollPos >= textLength - maxVisibleChars) scrollDirection = false;
        } else {
            scrollPos--;
            if (scrollPos <= 0) scrollDirection = true;
        }
    }

    int getX() const override { return _x; }
    int getY() const override { return _y; }
    int getWidth() const override { return _width; }
    int getHeight() const override { return _height; }

private:
    int _x, _y, _width, _height;
    int scrollPos;
    bool scrollDirection;
};

class Button : public UIComponent {
public:
    Button(int x, int y, int width, int height, const String &label, bool *state)
        : _x(x), _y(y), _width(width), _height(height), label(label), state(state), isActive(false) {}

    void draw(M5Canvas &sprite) override {
        sprite.fillRoundRect(_x, _y, _width, _height, 3, isActive ? GREEN : 0x5AEB);
        sprite.setTextColor(BLACK, isActive ? GREEN : 0x5AEB);
        sprite.drawString(label, _x + _width/2, _y + _height/2);
    }

    void setActive(bool active) override { isActive = active; }

    void increment() override {
        if (state) *state = !*state;
    }

    void decrement() override {
        if (state) *state = !*state;
    }

    int getX() const override { return _x; }
    int getY() const override { return _y; }
    int getWidth() const override { return _width; }
    int getHeight() const override { return _height; }

private:
    int _x, _y, _width, _height;
    String label;
    bool isActive;
    bool *state;
};

class Slider : public UIComponent {
public:
    Slider(int x, int y, int width, int height, int minVal, int maxVal, int *value)
        : _x(x), _y(y), _width(width), _height(height), minVal(minVal), maxVal(maxVal), value(value) {}

    void draw(M5Canvas &sprite) override {
        sprite.fillRoundRect(_x, _y, _width, _height, 2, 0x5AEB);
        int knobPos = map(*value, minVal, maxVal, _x, _x + _width);
        sprite.fillRoundRect(knobPos - 5, _y - 2, 10, _height + 4, 2, 0x2104);
    }

    void setValue(int newValue) {
        *value = constrain(newValue, minVal, maxVal);
    }

    void increment() override {
        setValue(*value + 1);
    }

    void decrement() override {
        setValue(*value - 1);
    }

    int getX() const override { return _x; }
    int getY() const override { return _y; }
    int getWidth() const override { return _width; }
    int getHeight() const override { return _height; }

protected:
    int _x, _y, _width, _height;
    int minVal, maxVal;
    int *value;
};

class SeekSlider : public Slider {
public:
    SeekSlider(int x, int y, int width, int height, int minVal, int maxVal, int *value, unsigned long *seekPosition, unsigned long duration, bool *isPlaying)
        : Slider(x, y, width, height, minVal, maxVal, value), seekPosition(seekPosition), duration(duration), isPlaying(isPlaying) {}

    void setValue(int newValue) override {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        *value = constrain(newValue, minVal, maxVal);
        *seekPosition = map(*value, minVal, maxVal, 0, duration);
        xSemaphoreGive(stateMutex);
    }

    void increment() override {
        setValue(*value + 1);
    }

    void decrement() override {
        setValue(*value - 1);
    }

private:
    unsigned long *seekPosition;
    unsigned long duration;
    bool *isPlaying;
};

void drawCursor(M5Canvas &sprite, int x, int y, int width, int height) {
    sprite.drawRect(x - 2, y - 2, width + 4, height + 4, WHITE);
    sprite.drawRect(x - 3, y - 3, width + 6, height + 6, WHITE);
}

// --- Audio Output ---
class AudioOutputM5CardputerSpeaker : public AudioOutput {
public:
    AudioOutputM5CardputerSpeaker(m5::Speaker_Class* m5sound) : _m5sound(m5sound) {}

    ~AudioOutputM5CardputerSpeaker() override {}

    bool begin() override { return true; }

    bool ConsumeSample(int16_t sample[2]) override {
        if (_tri_buffer_index < tri_buf_size) {
            _tri_buffer[_tri_index][_tri_buffer_index * 2] = sample[0];
            _tri_buffer[_tri_index][_tri_buffer_index * 2 + 1] = sample[1];
            _tri_buffer_index++;
            return true;
        }
        flush();
        return false;
    }

    void flush() override {
        if (_tri_buffer_index > 0) {
            uint32_t waitCount = 0;
            while (_m5sound->isPlaying() && waitCount < 1000) {
                vTaskDelay(1 / portTICK_PERIOD_MS);
                waitCount++;
            }
            if (_tri_buffer_index > 0) {
                _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index * 2, hertz, true);
            }
            _tri_index = (_tri_index < 2) ? _tri_index + 1 : 0;
            _tri_buffer_index = 0;
        }
    }

    bool stop() override {
        flush();
        while (_m5sound->isPlaying()) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        return true;
    }

    const int16_t* getBuffer() const {
        return _tri_buffer[(_tri_index + 2) % 3];
    }

private:
    m5::Speaker_Class* _m5sound;
    static constexpr size_t tri_buf_size = 1536;
    int16_t _tri_buffer[3][tri_buf_size * 2];
    size_t _tri_buffer_index = 0;
    size_t _tri_index = 0;
};

// --- Global State ---
std::vector<String> audioFiles;
std::unique_ptr<AudioFileSourceSD> audioFile;
std::unique_ptr<AudioFileSourceID3> audioId3;
std::unique_ptr<AudioGeneratorMP3> audioMp3;
std::unique_ptr<AudioOutputM5CardputerSpeaker> audioOut;
TaskHandle_t handleAudioTask = nullptr;
unsigned long trackStartTime = 0;
unsigned long trackElapsedSeconds = 0;
unsigned short grays[18];
const int brightnessLevels[5] = {50, 100, 150, 200, 250};

// --- UI Components ---
MyFFTWindow fftWindow(FFT_WINDOW_X, FFT_WINDOW_Y, FFT_WINDOW_WIDTH, FFT_WINDOW_HEIGHT);
ScrollingText scrollingText(FFT_WINDOW_X, FFT_WINDOW_Y - 10, FFT_WINDOW_WIDTH, 10);
Button playButton(148, 94, 18, 18, "A", &playerState.isPlaying);
Button prevButton(170, 94, 18, 18, "P", nullptr);
Button nextButton(192, 94, 18, 18, "N", nullptr);
Button randomButton(214, 94, 18, 18, "B", nullptr);
SeekSlider seekSlider(148, 60, 85, 8, 0, 100, &playerState.seekValue, &playerState.seekPosition, playerState.trackDuration, &playerState.isPlaying);
Slider volumeSlider(155, 80, 85, 8, 0, 20, &playerState.volume);
Slider brightnessSlider(172, 122, 30, 8, 0, 4, &playerState.brightnessIndex);

// --- Helper Functions ---
bool isMP3File(const String& filename) {
    String fn = filename;
    fn.toLowerCase();
    return fn.endsWith(".mp3") || fn.endsWith(".mpga") || fn.endsWith(".mp2") ||
           fn.endsWith(".m4a") || fn.endsWith(".m4p") || fn.endsWith(".mp4a");
}

void listMP3Files() {
    Serial.println("\n=== Listing MP3 files in /mp3 folder ===");
    audioFiles.clear();
    File mp3Folder = SD.open("/mp3");
    if (!mp3Folder) {
        Serial.println("Failed to open /mp3 folder");
        if (SD.mkdir("/mp3")) {
            Serial.println("Created /mp3 folder");
            mp3Folder = SD.open("/mp3");
        } else {
            Serial.println("Failed to create /mp3 folder");
            return;
        }
    }
    if (!mp3Folder.isDirectory()) {
        Serial.println("/mp3 is not a directory");
        mp3Folder.close();
        return;
    }
    mp3Folder.rewindDirectory();
    File file = mp3Folder.openNextFile();
    while (file && audioFiles.size() < 1000) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            if (isMP3File(filename)) {
                audioFiles.push_back("/mp3/" + filename);
            }
        }
        file.close();
        file = mp3Folder.openNextFile();
    }
    mp3Folder.close();
    Serial.printf("Found %d MP3 files.\n", audioFiles.size());
}

void resetClock() {
    trackStartTime = millis();
    trackElapsedSeconds = 0;
    playerState.seekPosition = 0;
    playerState.seekValue = 0;
}

String getTimeString() {
    unsigned long elapsed = trackElapsedSeconds;
    if (trackStartTime > 0 && !playerState.isStopped) {
        elapsed = (millis() - trackStartTime) / 1000;
    }
    int minutes = elapsed / 60;
    int seconds = elapsed % 60;
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", minutes, seconds);
    return String(timeStr);
}

void setup() {
    Serial.begin(115200);
    Serial.println("M5Mp3 Winamp Player for Cardputer-Adv - MP3 FOLDER VERSION");
    delay(500);
    resetClock();
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(brightnessLevels[playerState.brightnessIndex]);
    sprite.createSprite(240, 135);
    spr.createSprite(86, 16);
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS)) {
        Serial.println(F("ERROR: SD Mount Failed!"));
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.setCursor(10, 60);
        M5Cardputer.Display.println("SD Card Error!");
        while(1) delay(1000);
    }
    listMP3Files();
    if (audioFiles.empty()) {
        Serial.println("No MP3 files found in /mp3 folder!");
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.setCursor(10, 60);
        M5Cardputer.Display.println("No MP3 files in /mp3!");
        while(1) delay(1000);
    }
    M5Cardputer.Speaker.begin();
    uint8_t m5Volume = map(playerState.volume, 0, 20, 0, 255);
    M5Cardputer.Speaker.setVolume(m5Volume);
    audioOut = std::make_unique<AudioOutputM5CardputerSpeaker>(&M5Cardputer.Speaker);
    audioOut->SetGain(static_cast<float>(playerState.volume) / 20.0f);
    if (!audioFiles.empty()) {
        Serial.print("Initializing first file: ");
        Serial.println(audioFiles[playerState.currentTrack]);
        audioFile = std::make_unique<AudioFileSourceSD>(audioFiles[playerState.currentTrack].c_str());
        audioId3 = std::make_unique<AudioFileSourceID3>(audioFile.get());
        audioMp3 = std::make_unique<AudioGeneratorMP3>();
        audioMp3->begin(audioId3.get(), audioOut.get());
    }
    int co = 214;
    for (int i = 0; i < 18; i++) {
        grays[i] = M5Cardputer.Display.color565(co, co, co + 40);
        co = co - 13;
    }
    xTaskCreatePinnedToCore(Task_TFT, "Task_TFT", 20480, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(Task_Audio, "Task_Audio", 10240, nullptr, 3, &handleAudioTask, 1);
    Serial.println("Setup complete!");
}

void loop() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command == "t+") {
            playerState.textSize++;
            Serial.print("Text size increased to: ");
            Serial.println(playerState.textSize);
        } else if (command == "t-") {
            if (playerState.textSize > 1) {
                playerState.textSize--;
                Serial.print("Text size decreased to: ");
                Serial.println(playerState.textSize);
            } else {
                Serial.println("Text size is already at minimum.");
            }
        }
    }
}

void draw() {
    if (playerState.graphSpeed == 0) {
        unsigned short gray = grays[15];
        unsigned short light = grays[11];
        sprite.fillRect(0, 0, 240, 135, gray);
        sprite.fillRect(4, 8, 130, 122, BLACK);
        sprite.fillRect(129, 8, 5, 122, 0x0841);
        int sliderPos = map(playerState.currentTrack, 0, audioFiles.size(), 8, 110);
        sprite.fillRect(129, sliderPos, 5, 20, grays[2]);
        sprite.fillRect(131, sliderPos + 4, 1, 12, grays[16]);
        sprite.fillRect(4, 2, 50, 2, ORANGE);
        sprite.fillRect(84, 2, 50, 2, ORANGE);
        sprite.fillRect(190, 2, 45, 2, ORANGE);
        sprite.fillRect(190, 6, 45, 3, grays[4]);
        sprite.drawFastVLine(3, 9, 120, light);
        sprite.drawFastVLine(134, 9, 120, light);
        sprite.drawFastHLine(3, 129, 130, light);
        sprite.drawFastHLine(0, 0, 240, light);
        sprite.drawFastHLine(0, 134, 240, light);
        sprite.fillRect(139, 0, 3, 135, BLACK);
        sprite.fillRect(148, 14, 86, 42, BLACK);
        sprite.fillRect(148, 59, 86, 16, BLACK);
        sprite.fillTriangle(162, 18, 162, 26, 168, 22, GREEN);
        sprite.fillRect(162, 30, 6, 6, RED);
        sprite.drawFastVLine(143, 0, 135, light);
        sprite.drawFastVLine(238, 0, 135, light);
        sprite.drawFastVLine(138, 0, 135, light);
        sprite.drawFastVLine(148, 14, 42, light);
        sprite.drawFastHLine(148, 14, 86, light);
        for (int i = 0; i < 4; i++)
            sprite.fillRoundRect(148 + (i * 22), 94, 18, 18, 3, grays[4]);
        sprite.fillRect(220, 104, 8, 2, grays[13]);
        sprite.fillRect(220, 108, 8, 2, grays[13]);
        sprite.fillTriangle(228, 102, 228, 106, 231, 105, grays[13]);
        sprite.fillTriangle(220, 106, 220, 110, 217, 109, grays[13]);
        if (!playerState.isStopped) {
            sprite.fillRect(152, 104, 3, 6, grays[13]);
            sprite.fillRect(157, 104, 3, 6, grays[13]);
        } else {
            sprite.fillTriangle(156, 102, 156, 110, 160, 106, grays[13]);
        }
        sprite.fillRoundRect(172, 82, 60, 3, 2, YELLOW);
        sprite.fillRoundRect(155 + ((playerState.volume / 5) * 17), 80, 10, 8, 2, grays[2]);
        sprite.fillRoundRect(157 + ((playerState.volume / 5) * 17), 82, 6, 4, 2, grays[10]);
        sprite.fillRoundRect(172, 124, 30, 3, 2, MAGENTA);
        sprite.fillRoundRect(172 + (playerState.brightnessIndex * 5), 122, 10, 8, 2, grays[2]);
        sprite.fillRoundRect(174 + (playerState.brightnessIndex * 5), 124, 6, 4, 2, grays[10]);
        sprite.drawRect(206, 119, 28, 12, GREEN);
        sprite.fillRect(234, 122, 3, 6, GREEN);
        sprite.setTextFont(0);
        sprite.setTextSize(playerState.textSize);
        sprite.setTextDatum(0);
        int visibleItems = 10;
        int startIndex = std::max(0, playerState.currentTrack - (visibleItems / 2));
        if (startIndex + visibleItems > audioFiles.size()) {
            startIndex = std::max(0, static_cast<int>(audioFiles.size()) - visibleItems);
        }
        for (int i = 0; i < visibleItems; i++) {
            int fileIndex = startIndex + i;
            if (fileIndex >= 0 && fileIndex < audioFiles.size()) {
                if (fileIndex == playerState.currentTrack) {
                    sprite.setTextColor(WHITE, BLACK);
                } else {
                    sprite.setTextColor(GREEN, BLACK);
                }
                String filename = audioFiles[fileIndex];
                int lastSlash = filename.lastIndexOf('/');
                if (lastSlash >= 0) {
                    filename = filename.substring(lastSlash + 1);
                }
                int displayLength = 20;
                if (filename.length() > displayLength) {
                    sprite.drawString(filename.substring(0, displayLength), 8, 10 + (i * 12 * playerState.textSize));
                } else {
                    sprite.drawString(filename, 8, 10 + (i * 12 * playerState.textSize));
                }
            }
        }
        sprite.setTextColor(grays[1], gray);
        sprite.setTextSize(playerState.textSize);
        sprite.drawString("WINAMP", 150, 4);
        sprite.setTextColor(grays[2], gray);
        sprite.drawString("LIST", 58, 0);
        sprite.setTextColor(grays[4], gray);
        sprite.drawString("SEEK", 150, 58);
        sprite.drawString("VOL", 150, 80);
        sprite.drawString("LIG", 150, 122);
        if (playerState.isPlaying) {
            sprite.setTextColor(grays[8], BLACK);
            sprite.drawString("P", 152, 18);
            sprite.drawString("L", 152, 27);
            sprite.drawString("A", 152, 36);
            sprite.drawString("Y", 152, 45);
        } else {
            sprite.setTextColor(grays[8], BLACK);
            sprite.drawString("S", 152, 18);
            sprite.drawString("T", 152, 27);
            sprite.drawString("O", 152, 36);
            sprite.drawString("P", 152, 45);
        }
        sprite.setTextColor(GREEN, BLACK);
        if (!playerState.isStopped) {
            if (trackStartTime > 0) {
                trackElapsedSeconds = (millis() - trackStartTime) / 1000;
            }
            sprite.drawString(getTimeString(), 172, 18);
        }
        sprite.setTextFont(0);
        int percent = 0;
        float batteryVoltage = M5Cardputer.Power.getBatteryVoltage();
        if (batteryVoltage > 4.2)
            percent = 100;
        else if (batteryVoltage < 3.0)
            percent = 1;
        else
            percent = map(static_cast<int>(batteryVoltage * 100), 300, 420, 1, 100);
        sprite.setTextDatum(3);
        sprite.setTextSize(playerState.textSize);
        sprite.drawString(String(percent) + "%", 220, 121);
        sprite.setTextColor(BLACK, grays[4]);
        sprite.setTextSize(playerState.textSize);
        sprite.drawString("B", 220, 96);
        sprite.drawString("N", 198, 96);
        sprite.drawString("P", 176, 96);
        sprite.drawString("A", 154, 96);
        sprite.setTextColor(BLACK, grays[5]);
        sprite.drawString(">>", 202, 103);
        sprite.drawString("<<", 180, 103);

        // Draw UI components
        fftWindow.draw(sprite);
        if (!playerState.isStopped && playerState.currentTrack < audioFiles.size()) {
            String filename = audioFiles[playerState.currentTrack];
            int lastSlash = filename.lastIndexOf('/');
            if (lastSlash >= 0) {
                filename = filename.substring(lastSlash + 1);
            }
            scrollingText.draw(sprite, filename);
        }
        seekSlider.draw(sprite);
        playButton.draw(sprite);
        prevButton.draw(sprite);
        nextButton.draw(sprite);
        randomButton.draw(sprite);
        volumeSlider.draw(sprite);
        brightnessSlider.draw(sprite);

        // Highlight active control if cursor is on the right
        if (playerState.cursorOnRight) {
            switch (playerState.activeControl) {
                case PlayerState::SEEK: {
                    drawCursor(sprite, seekSlider.getX(), seekSlider.getY(), seekSlider.getWidth(), seekSlider.getHeight());
                    break;
                }
                case PlayerState::VOLUME: {
                    drawCursor(sprite, volumeSlider.getX(), volumeSlider.getY(), volumeSlider.getWidth(), volumeSlider.getHeight());
                    break;
                }
                case PlayerState::BRIGHTNESS: {
                    drawCursor(sprite, brightnessSlider.getX(), brightnessSlider.getY(), brightnessSlider.getWidth(), brightnessSlider.getHeight());
                    break;
                }
                case PlayerState::BUTTON_A: {
                    playButton.setActive(true);
                    drawCursor(sprite, playButton.getX(), playButton.getY(), playButton.getWidth(), playButton.getHeight());
                    break;
                }
                case PlayerState::BUTTON_P: {
                    prevButton.setActive(true);
                    drawCursor(sprite, prevButton.getX(), prevButton.getY(), prevButton.getWidth(), prevButton.getHeight());
                    break;
                }
                case PlayerState::BUTTON_N: {
                    nextButton.setActive(true);
                    drawCursor(sprite, nextButton.getX(), nextButton.getY(), nextButton.getWidth(), nextButton.getHeight());
                    break;
                }
                case PlayerState::BUTTON_B: {
                    randomButton.setActive(true);
                    drawCursor(sprite, randomButton.getX(), randomButton.getY(), randomButton.getWidth(), randomButton.getHeight());
                    break;
                }
            }
        }

        sprite.pushSprite(0, 0);
    }
    playerState.graphSpeed++;
    if (playerState.graphSpeed == 4) playerState.graphSpeed = 0;
}

void Task_TFT(void *pvParameters) {
    while (1) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange()) {
            if (M5Cardputer.Keyboard.isKeyPressed('/')) {
                xSemaphoreTake(stateMutex, portMAX_DELAY);
                playerState.cursorOnRight = !playerState.cursorOnRight;
                xSemaphoreGive(stateMutex);
            }
            if (playerState.cursorOnRight) {
                if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    playerState.activeControl = static_cast<PlayerState::Control>(static_cast<int>(playerState.activeControl) - 1);
                    if (playerState.activeControl < 0) playerState.activeControl = PlayerState::BUTTON_B;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    playerState.activeControl = static_cast<PlayerState::Control>(static_cast<int>(playerState.activeControl) + 1);
                    if (playerState.activeControl > PlayerState::BUTTON_B) playerState.activeControl = PlayerState::SEEK;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('-')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    switch (playerState.activeControl) {
                        case PlayerState::SEEK: seekSlider.decrement(); break;
                        case PlayerState::VOLUME: volumeSlider.decrement(); playerState.volumeUpdated = true; break;
                        case PlayerState::BRIGHTNESS: brightnessSlider.decrement(); M5Cardputer.Display.setBrightness(brightnessLevels[playerState.brightnessIndex]); break;
                        default: break;
                    }
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('=')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    switch (playerState.activeControl) {
                        case PlayerState::SEEK: seekSlider.increment(); break;
                        case PlayerState::VOLUME: volumeSlider.increment(); playerState.volumeUpdated = true; break;
                        case PlayerState::BRIGHTNESS: brightnessSlider.increment(); M5Cardputer.Display.setBrightness(brightnessLevels[playerState.brightnessIndex]); break;
                        default: break;
                    }
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    switch (playerState.activeControl) {
                        case PlayerState::BUTTON_A: playButton.increment(); break;
                        case PlayerState::BUTTON_P:
                            resetClock();
                            playerState.isPlaying = false;
                            playerState.currentTrack--;
                            if (playerState.currentTrack < 0) playerState.currentTrack = audioFiles.size() - 1;
                            playerState.nextTrackRequested = true;
                            break;
                        case PlayerState::BUTTON_N:
                            resetClock();
                            playerState.isPlaying = false;
                            playerState.currentTrack++;
                            if (playerState.currentTrack >= audioFiles.size()) playerState.currentTrack = 0;
                            playerState.nextTrackRequested = true;
                            break;
                        case PlayerState::BUTTON_B:
                            resetClock();
                            playerState.isPlaying = false;
                            playerState.currentTrack = random(0, audioFiles.size());
                            playerState.nextTrackRequested = true;
                            break;
                        default: break;
                    }
                    xSemaphoreGive(stateMutex);
                }
            } else {
                if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    playerState.currentTrack--;
                    if (playerState.currentTrack < 0) playerState.currentTrack = audioFiles.size() - 1;
                    playerState.nextTrackRequested = true;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    playerState.currentTrack++;
                    if (playerState.currentTrack >= audioFiles.size()) playerState.currentTrack = 0;
                    playerState.nextTrackRequested = true;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('a')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    playerState.isPlaying = !playerState.isPlaying;
                    playerState.isStopped = !playerState.isStopped;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('v')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    playerState.volume++;
                    if (playerState.volume > 20) playerState.volume = 0;
                    playerState.volumeUpdated = true;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('l')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    playerState.brightnessIndex++;
                    if (playerState.brightnessIndex == 5) playerState.brightnessIndex = 0;
                    M5Cardputer.Display.setBrightness(brightnessLevels[playerState.brightnessIndex]);
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('n')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    resetClock();
                    playerState.isPlaying = false;
                    playerState.currentTrack++;
                    if (playerState.currentTrack >= audioFiles.size()) playerState.currentTrack = 0;
                    playerState.nextTrackRequested = true;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('p')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    resetClock();
                    playerState.isPlaying = false;
                    playerState.currentTrack--;
                    if (playerState.currentTrack < 0) playerState.currentTrack = audioFiles.size() - 1;
                    playerState.nextTrackRequested = true;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    resetClock();
                    playerState.isStopped = false;
                    playerState.isPlaying = true;
                    xSemaphoreGive(stateMutex);
                }
                if (M5Cardputer.Keyboard.isKeyPressed('b')) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    resetClock();
                    playerState.isPlaying = false;
                    playerState.currentTrack = random(0, audioFiles.size());
                    playerState.nextTrackRequested = true;
                    xSemaphoreGive(stateMutex);
                }
            }
        }
        draw();
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}

void Task_Audio(void *pvParameters) {
    while (1) {
        if (playerState.volumeUpdated) {
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            if (audioOut) {
                audioOut->SetGain(static_cast<float>(playerState.volume) / 20.0f);
            }
            uint8_t m5Volume = map(playerState.volume, 0, 20, 0, 255);
            M5Cardputer.Speaker.setVolume(m5Volume);
            playerState.isPlaying = true;
            playerState.volumeUpdated = false;
            xSemaphoreGive(stateMutex);
        }
        if (playerState.nextTrackRequested) {
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            if (audioMp3 && audioMp3->isRunning()) {
                audioMp3->stop();
            }
            audioId3.reset();
            audioFile.reset();
            if (playerState.currentTrack < audioFiles.size()) {
                audioFile = std::make_unique<AudioFileSourceSD>(audioFiles[playerState.currentTrack].c_str());
                audioId3 = std::make_unique<AudioFileSourceID3>(audioFile.get());
                audioMp3 = std::make_unique<AudioGeneratorMP3>();
                audioMp3->begin(audioId3.get(), audioOut.get());
            }
            playerState.isPlaying = true;
            playerState.nextTrackRequested = false;
            xSemaphoreGive(stateMutex);
        }
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        if (playerState.isPlaying && audioMp3) {
            if (!playerState.isStopped) {
                if (audioMp3->isRunning()) {
                    if (!audioMp3->loop()) {
                        resetClock();
                        playerState.currentTrack++;
                        if (playerState.currentTrack >= audioFiles.size()) playerState.currentTrack = 0;
                        playerState.nextTrackRequested = true;
                    }
                } else {
                    resetClock();
                    playerState.currentTrack++;
                    if (playerState.currentTrack >= audioFiles.size()) playerState.currentTrack = 0;
                    playerState.nextTrackRequested = true;
                }
            }
        }
        xSemaphoreGive(stateMutex);
        // --- FFT Computation ---
        if (millis() - lastFFTTime > 50) { // Update FFT every 50ms
            lastFFTTime = millis();
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            if (audioOut) {
                const int16_t* buffer = audioOut->getBuffer();
                if (buffer) {
                    for (int i = 0; i < FFT_SIZE; i++) {
                        vReal[i] = buffer[i * 2] / 32768.0; // Normalize to [-1, 1]
                        vImag[i] = 0;
                    }
                    FFT.windowing(vReal, FFT_SIZE, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
                    FFT.compute(vReal, vImag, FFT_SIZE, FFT_FORWARD);
                    FFT.complexToMagnitude(vReal, vImag, FFT_SIZE);
                    for (int i = 0; i < FFT_SIZE / 2; i++) {
                        fftOutput[i] = vReal[i];
                    }
                }
            }
            xSemaphoreGive(stateMutex);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>
#include "Button.h"

#define I2S_PORT I2S_NUM_0

#define I2S_BCLK 4
#define I2S_LRCK 5
#define I2S_DIN  6   // mic
#define I2S_DOUT 7   // amp

#define BUTTON_PIN 9

#define RX_PIN 20
#define TX_PIN 21
#define DIR_PIN 10
const gpio_num_t CMD_BTN_PIN = GPIO_NUM_3;

#define LED_PIN GPIO_NUM_2
bool isLedOn = false;

HardwareSerial RS485(1);

#define SAMPLE_RATE 16000
#define BUFFER_SAMPLES (16000 * 3) // 3 seconds

int32_t *audioBuffer;
size_t recordedSamples = 0;
bool recording = false;

float GAIN = 15.0;

void setupI2S() {
    i2s_config_t config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false
    };

    i2s_pin_config_t pins = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRCK,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_DIN
    };

    i2s_driver_install(I2S_PORT, &config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pins);
}

void setTransmitMode() {
    digitalWrite(DIR_PIN, HIGH);
    Serial.println("TX MODE");
    delayMicroseconds(50);
}

void setReceiveMode() {
    delayMicroseconds(50);
    digitalWrite(DIR_PIN, LOW);
    Serial.println("RX MODE");
    delayMicroseconds(50);
}

void sendMessage(const char* msg) {
    Serial.println("--- sending: " + String(msg));
    setTransmitMode();

    RS485.print(msg);
    RS485.flush();

    delayMicroseconds(10);
    setReceiveMode();
}

void rs485Task(void *param) {
    while (true) {
        while (RS485.available()) {
            String msg = RS485.readStringUntil('\n');
            Serial.print("--- received: ");
            Serial.println(msg);
            if (isLedOn) {
                digitalWrite(LED_PIN, LOW);
                isLedOn = false;
            } else {
                digitalWrite(LED_PIN, HIGH);
                isLedOn = true;
            }
        }

        vTaskDelay(1);
    }
}

static void onCommandButtonPressDownCb(void *button_handle, void *usr_data) {
    Serial.println("--- CMD BTN DOWN ...");
    sendMessage("button pressed\n");
}

void initButtons() {
    Button *btnCmd = new Button(CMD_BTN_PIN, false);
    btnCmd->attachPressDownEventCb(&onCommandButtonPressDownCb, NULL);
}

void initRS485() {
    pinMode(DIR_PIN, OUTPUT);
    setReceiveMode();

    Serial.begin(115200);
    RS485.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.begin(115200);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    audioBuffer = (int32_t*)malloc(BUFFER_SAMPLES * sizeof(int32_t));
    if (!audioBuffer) {
        Serial.println("Memory allocation failed!");
        while (1);
    }

    setupI2S();
    initButtons();
    initRS485();
    xTaskCreatePinnedToCore(rs485Task, "rs485Task", 4096, NULL, 1, NULL, 0);
}

void loop() {
    bool buttonPressed = digitalRead(BUTTON_PIN) == LOW;

    if (buttonPressed && !recording) {
        Serial.println("Recording...");
        recording = true;
        recordedSamples = 0;
    }

    if (recording && buttonPressed) {
        if (recordedSamples < BUFFER_SAMPLES) {
            int32_t rawSample;
            size_t bytesRead;

            i2s_read(I2S_PORT, &rawSample, sizeof(rawSample), &bytesRead, portMAX_DELAY);
            rawSample >>= 8;
            float x = rawSample / 8388608.0;
            x *= GAIN;
            x = x / (1.0 + fabs(x));

            int32_t sample32 = (int32_t)(x * 2147483647);
            audioBuffer[recordedSamples++] = sample32;
        }
    }

    // ⏹ STOP → PLAYBACK
    if (!buttonPressed && recording) {
        Serial.println("Playback...");
        recording = false;

        size_t bytesWritten;

        for (size_t i = 0; i < recordedSamples; i++) {
            i2s_write(I2S_PORT, &audioBuffer[i], sizeof(int32_t), &bytesWritten, portMAX_DELAY);
        }

        i2s_zero_dma_buffer(I2S_PORT);
        i2s_stop(I2S_PORT);
        delay(10);
        i2s_start(I2S_PORT);

        Serial.println("Done");
    }
}
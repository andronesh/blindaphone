#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>

#define I2S_PORT I2S_NUM_0

#define I2S_BCLK 4
#define I2S_LRCK 5
#define I2S_DIN  6   // mic
#define I2S_DOUT 7   // amp

#define BUTTON_PIN 9

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

void setup() {
    Serial.begin(115200);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    audioBuffer = (int32_t*)malloc(BUFFER_SAMPLES * sizeof(int32_t));
    if (!audioBuffer) {
        Serial.println("Memory allocation failed!");
        while (1);
    }

    setupI2S();
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
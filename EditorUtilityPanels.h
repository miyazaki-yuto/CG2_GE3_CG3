#pragma once

class AudioManager;
class Graphics;
class LightingManager;

class PerformancePanel {
public:
    void Draw();
};

class LightingPanel {
public:
    void Draw(Graphics* graphics, LightingManager* lightingManager);
};

class SoundPanel {
public:
    void Draw(
        AudioManager* audioManager,
        int bgmHandle,
        float* bgmVolume,
        bool isPlaying);
};

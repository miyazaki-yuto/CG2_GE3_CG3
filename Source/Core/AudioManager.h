#pragma once

#include <wrl.h>
#include <xaudio2.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// XAudio2の初期化、WAV読み込み、再生Voiceをまとめて管理するクラス。
// 読み込んだ音声には整数ハンドルを割り当て、ゲーム側はハンドルで操作する。
class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager() = default;

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // XAudio2本体と、最終的な音声出力先となるMasteringVoiceを作成する。
    bool Initialize();

    // PCMまたはXAudio2が対応する形式のWAVを読み込み、音声ハンドルを返す。
    // 読み込みに失敗した場合は-1を返す。同じパスはキャッシュされる。
    int LoadWave(const std::string& filePath);

    // loop=trueの場合は、Stopが呼ばれるまで音声を繰り返す。
    bool Play(int soundHandle, bool loop = false);
    void Stop(int soundHandle);
    void Pause(int soundHandle);
    void Resume(int soundHandle);
    void SetVolume(int soundHandle, float volume);
    void SetMasterVolume(float volume);

    bool IsPlaying(int soundHandle) const;
    bool IsPaused(int soundHandle) const;
    const std::string& GetLastError() const { return lastError_; }

private:
    // XAudio2のVoiceはCOMではないためRelease()を持たず、DestroyVoice()で破棄する。
    // unique_ptrのカスタムデリータにすることで、終了時の呼び忘れを防ぐ。
    struct VoiceDeleter {
        template <class T>
        void operator()(T* voice) const noexcept {
            if (voice != nullptr) {
                voice->DestroyVoice();
            }
        }
    };

    using MasteringVoicePtr =
        std::unique_ptr<IXAudio2MasteringVoice, VoiceDeleter>;
    using SourceVoicePtr =
        std::unique_ptr<IXAudio2SourceVoice, VoiceDeleter>;

    struct Sound {
        // WAVEFORMATEXTENSIBLEは先頭にWAVEFORMATEXを持つため、通常のPCMにも使える。
        WAVEFORMATEXTENSIBLE waveFormat{};
        std::vector<uint8_t> audioData;
        SourceVoicePtr sourceVoice;
        bool isPaused = false;
    };

    bool LoadWaveFile(const std::string& filePath, Sound& sound);
    Sound* GetSound(int soundHandle);
    const Sound* GetSound(int soundHandle) const;
    void SetError(const std::string& message);

    // 宣言と逆順に破棄されるため、SoundのSourceVoice → MasteringVoice → XAudio2本体
    // の安全な順番で自動解放される。
    Microsoft::WRL::ComPtr<IXAudio2> xaudio2_;
    MasteringVoicePtr masteringVoice_;
    std::vector<std::unique_ptr<Sound>> sounds_;
    std::unordered_map<std::string, int> soundCache_;
    std::string lastError_;
};

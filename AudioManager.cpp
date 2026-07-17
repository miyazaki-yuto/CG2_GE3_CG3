#include "AudioManager.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

namespace {

// std::stringのパスはプロジェクト全体と同じくUTF-8として扱う。
std::filesystem::path MakePathFromUtf8(const std::string& text) {
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    const std::u8string utf8(first, first + text.size());
    return std::filesystem::path(utf8);
}

bool IsFourCC(const std::array<char, 4>& value, const char (&expected)[5]) {
    return std::memcmp(value.data(), expected, value.size()) == 0;
}

template <class T>
bool ReadBinary(std::ifstream& file, T& value) {
    return static_cast<bool>(file.read(
        reinterpret_cast<char*>(&value),
        static_cast<std::streamsize>(sizeof(T))));
}

bool ReadFourCC(std::ifstream& file, std::array<char, 4>& value) {
    return static_cast<bool>(file.read(
        value.data(),
        static_cast<std::streamsize>(value.size())));
}

} // namespace

bool AudioManager::Initialize() {
    // IXAudio2はCOMインターフェースなので、ComPtrへ直接受け取る。
    HRESULT hr = XAudio2Create(
        xaudio2_.ReleaseAndGetAddressOf(),
        0,
        XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        SetError("XAudio2Create failed.");
        return false;
    }

    // MasteringVoiceはスピーカーなど、OSが選んだ既定の出力デバイスへ音を送る。
    IXAudio2MasteringVoice* masteringVoice = nullptr;
    hr = xaudio2_->CreateMasteringVoice(&masteringVoice);
    if (FAILED(hr)) {
        SetError("CreateMasteringVoice failed.");
        xaudio2_.Reset();
        return false;
    }
    masteringVoice_.reset(masteringVoice);
    lastError_.clear();
    return true;
}

int AudioManager::LoadWave(const std::string& filePath) {
    if (xaudio2_ == nullptr) {
        SetError("AudioManager is not initialized.");
        return -1;
    }

    const auto cacheIterator = soundCache_.find(filePath);
    if (cacheIterator != soundCache_.end()) {
        return cacheIterator->second;
    }

    auto sound = std::make_unique<Sound>();
    if (!LoadWaveFile(filePath, *sound)) {
        return -1;
    }

    // WAVのフォーマットと同じSourceVoiceを作り、再生時に音声バッファを送る。
    IXAudio2SourceVoice* sourceVoice = nullptr;
    const HRESULT hr = xaudio2_->CreateSourceVoice(
        &sourceVoice,
        &sound->waveFormat.Format);
    if (FAILED(hr)) {
        SetError("CreateSourceVoice failed: " + filePath);
        return -1;
    }
    sound->sourceVoice.reset(sourceVoice);

    if (sounds_.size() >=
        static_cast<size_t>((std::numeric_limits<int>::max)())) {
        SetError("Too many sounds have been loaded.");
        return -1;
    }

    const int soundHandle = static_cast<int>(sounds_.size());
    sounds_.push_back(std::move(sound));
    soundCache_.emplace(filePath, soundHandle);
    lastError_.clear();
    return soundHandle;
}

bool AudioManager::LoadWaveFile(const std::string& filePath, Sound& sound) {
    std::ifstream file{ MakePathFromUtf8(filePath), std::ios::binary };
    if (!file.is_open()) {
        SetError("WAV file could not be opened: " + filePath);
        return false;
    }

    // RIFFヘッダーは "RIFF"、ファイルサイズ、"WAVE" の順に並ぶ。
    std::array<char, 4> riffId{};
    uint32_t riffSize = 0;
    std::array<char, 4> waveId{};
    if (!ReadFourCC(file, riffId) ||
        !ReadBinary(file, riffSize) ||
        !ReadFourCC(file, waveId) ||
        !IsFourCC(riffId, "RIFF") ||
        !IsFourCC(waveId, "WAVE")) {
        SetError("Invalid RIFF/WAVE header: " + filePath);
        return false;
    }
    // 現在はファイルストリーム自身が終端を検証するので、RIFFサイズは参照だけにする。
    static_cast<void>(riffSize);

    bool foundFormat = false;
    bool foundData = false;
    while (file && (!foundFormat || !foundData)) {
        std::array<char, 4> chunkId{};
        uint32_t chunkSize = 0;
        if (!ReadFourCC(file, chunkId) || !ReadBinary(file, chunkSize)) {
            break;
        }

        if (IsFourCC(chunkId, "fmt ")) {
            // PCMのfmtは16バイト、拡張形式でもWAVEFORMATEXTENSIBLE内に収まる。
            constexpr uint32_t kMinimumWaveFormatSize = 16;
            if (chunkSize < kMinimumWaveFormatSize ||
                chunkSize > sizeof(WAVEFORMATEXTENSIBLE)) {
                SetError("Unsupported WAV format chunk: " + filePath);
                return false;
            }

            sound.waveFormat = {};
            if (!file.read(
                reinterpret_cast<char*>(&sound.waveFormat),
                static_cast<std::streamsize>(chunkSize))) {
                SetError("WAV format chunk is truncated: " + filePath);
                return false;
            }
            foundFormat = true;
        } else if (IsFourCC(chunkId, "data")) {
            if (chunkSize == 0) {
                SetError("WAV data chunk is empty: " + filePath);
                return false;
            }
            sound.audioData.resize(chunkSize);
            if (!file.read(
                reinterpret_cast<char*>(sound.audioData.data()),
                static_cast<std::streamsize>(chunkSize))) {
                SetError("WAV data chunk is truncated: " + filePath);
                return false;
            }
            foundData = true;
        } else {
            // LISTなど、再生に使わないチャンクは読み飛ばす。
            file.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
        }

        // RIFFチャンクは2バイト境界に配置される。サイズが奇数ならパディングを1バイト飛ばす。
        if ((chunkSize & 1u) != 0u) {
            file.seekg(1, std::ios::cur);
        }
    }

    if (!foundFormat || !foundData) {
        SetError("WAV fmt or data chunk was not found: " + filePath);
        return false;
    }
    if (sound.waveFormat.Format.wFormatTag == 0 ||
        sound.waveFormat.Format.nChannels == 0 ||
        sound.waveFormat.Format.nSamplesPerSec == 0) {
        SetError("WAV format values are invalid: " + filePath);
        return false;
    }
    return true;
}

bool AudioManager::Play(int soundHandle, bool loop) {
    Sound* sound = GetSound(soundHandle);
    if (sound == nullptr || sound->sourceVoice == nullptr) {
        return false;
    }

    // 再生中の同じ音を先頭から鳴らし直せるよう、前のバッファを空にする。
    HRESULT hr = sound->sourceVoice->Stop();
    if (FAILED(hr)) {
        SetError("SourceVoice Stop failed before Play.");
        return false;
    }
    hr = sound->sourceVoice->FlushSourceBuffers();
    if (FAILED(hr)) {
        SetError("FlushSourceBuffers failed.");
        return false;
    }

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(sound->audioData.size());
    buffer.pAudioData = sound->audioData.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    hr = sound->sourceVoice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        SetError("SubmitSourceBuffer failed.");
        return false;
    }
    hr = sound->sourceVoice->Start();
    if (FAILED(hr)) {
        sound->sourceVoice->FlushSourceBuffers();
        SetError("SourceVoice Start failed.");
        return false;
    }

    sound->isPaused = false;
    lastError_.clear();
    return true;
}

void AudioManager::Stop(int soundHandle) {
    Sound* sound = GetSound(soundHandle);
    if (sound == nullptr || sound->sourceVoice == nullptr) {
        return;
    }
    sound->sourceVoice->Stop();
    sound->sourceVoice->FlushSourceBuffers();
    sound->isPaused = false;
}

void AudioManager::Pause(int soundHandle) {
    Sound* sound = GetSound(soundHandle);
    if (sound == nullptr || sound->sourceVoice == nullptr ||
        !IsPlaying(soundHandle)) {
        return;
    }
    if (SUCCEEDED(sound->sourceVoice->Stop())) {
        // Flushしないため、Resumeで現在位置から続きを再生できる。
        sound->isPaused = true;
    }
}

void AudioManager::Resume(int soundHandle) {
    Sound* sound = GetSound(soundHandle);
    if (sound == nullptr || sound->sourceVoice == nullptr || !sound->isPaused) {
        return;
    }
    if (SUCCEEDED(sound->sourceVoice->Start())) {
        sound->isPaused = false;
    }
}

void AudioManager::SetVolume(int soundHandle, float volume) {
    Sound* sound = GetSound(soundHandle);
    if (sound == nullptr || sound->sourceVoice == nullptr) {
        return;
    }
    sound->sourceVoice->SetVolume((std::max)(0.0f, volume));
}

void AudioManager::SetMasterVolume(float volume) {
    if (masteringVoice_ != nullptr) {
        masteringVoice_->SetVolume((std::max)(0.0f, volume));
    }
}

bool AudioManager::IsPlaying(int soundHandle) const {
    const Sound* sound = GetSound(soundHandle);
    if (sound == nullptr || sound->sourceVoice == nullptr || sound->isPaused) {
        return false;
    }

    XAUDIO2_VOICE_STATE state{};
    sound->sourceVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued > 0;
}

bool AudioManager::IsPaused(int soundHandle) const {
    const Sound* sound = GetSound(soundHandle);
    return sound != nullptr && sound->isPaused;
}

AudioManager::Sound* AudioManager::GetSound(int soundHandle) {
    if (soundHandle < 0 ||
        static_cast<size_t>(soundHandle) >= sounds_.size()) {
        return nullptr;
    }
    return sounds_[static_cast<size_t>(soundHandle)].get();
}

const AudioManager::Sound* AudioManager::GetSound(int soundHandle) const {
    if (soundHandle < 0 ||
        static_cast<size_t>(soundHandle) >= sounds_.size()) {
        return nullptr;
    }
    return sounds_[static_cast<size_t>(soundHandle)].get();
}

void AudioManager::SetError(const std::string& message) {
    lastError_ = message;
    OutputDebugStringA((message + "\n").c_str());
}

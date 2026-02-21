#pragma once

#include <vector>

class Mp3Encoder
{
public:
    Mp3Encoder();
    void openFile();
    void PcmResample();
    void saveFileAsPcm();

private:
    std::vector<uint8_t> pcmData{};
};

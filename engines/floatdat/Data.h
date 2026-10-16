// SEMI port: FLOATDAT's data loader (Source/data/DataLoader.*) without JUCE. Bytes -> DataBlob (READ AS
// AUTO / BYTES / TEXT, the analysis), decoded audio -> DataBlob (AUDIO; the module decodes WAV with the
// series' reader), the nine example texts. IMAGE has no decoder here: such files are read as bytes, with
// the plug-in's own fallback note.
#pragma once

#include "DataBlob.h"
#include <memory>
#include <string>
#include <vector>

namespace fdat_pdata
{
const std::vector<std::string>& readAsNames();   // auto bytes text audio image

// requested AUTO picks AUDIO for audio types only when `audio` is given (decoded, mono, -1..1)
void build (DataBlob& b, ReadAs requested, const std::vector<float>* audio = nullptr, double audioRate = 0.0, int audioChannels = 0);

bool fromBytes (DataBlob& b, std::vector<uint8_t> bytes, const std::string& name, const std::string& path,
                uint64_t fileSize, ReadAs requested, const std::vector<float>* audio = nullptr, double rate = 0.0, int channels = 0);
bool fromText (DataBlob& b, const std::string& text, const std::string& name, ReadAs requested = ReadAs::Auto);

std::string exampleText (int index);   // 1..9
std::string exampleName (int index);
bool isAudioType (const std::string& typeName);
} // namespace fdat_pdata

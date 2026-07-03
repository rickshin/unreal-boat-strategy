#pragma once

#include "CoreMinimal.h"

// Decoded PCM for one soundtrack file (interleaved signed 16-bit).
struct FDecodedMusic
{
	TArray<uint8> PCM;
	int32 SampleRate = 44100;
	int32 Channels = 2;
	float Duration = 0.f;
};

// Decodes an .mp3 from disk using the vendored minimp3 (ThirdParty/, CC0).
// Safe to call off the game thread. Returns false on failure.
bool DecodeMp3File(const FString& FilePath, FDecodedMusic& Out);

#include "MusicDecoder.h"
#include "Misc/FileHelper.h"

// minimp3 (github.com/lieff/minimp3, CC0): single-header MP3 decoder.
// Implementation lives only in this translation unit.
THIRD_PARTY_INCLUDES_START
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "ThirdParty/minimp3_ex.h"
THIRD_PARTY_INCLUDES_END

bool DecodeMp3File(const FString& FilePath, FDecodedMusic& Out)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		return false;
	}

	mp3dec_t Decoder;
	mp3dec_file_info_t Info = {};
	if (mp3dec_load_buf(&Decoder, FileData.GetData(), size_t(FileData.Num()), &Info, nullptr, nullptr) != 0
		|| Info.buffer == nullptr || Info.samples == 0 || Info.channels <= 0 || Info.hz <= 0)
	{
		if (Info.buffer) { free(Info.buffer); }
		return false;
	}

	// Info.samples counts individual samples across all channels.
	Out.SampleRate = Info.hz;
	Out.Channels = Info.channels;
	Out.PCM.SetNumUninitialized(int64(Info.samples) * sizeof(mp3d_sample_t));
	FMemory::Memcpy(Out.PCM.GetData(), Info.buffer, Out.PCM.Num());
	Out.Duration = float(Info.samples / Info.channels) / float(Info.hz);
	free(Info.buffer);
	return true;
}

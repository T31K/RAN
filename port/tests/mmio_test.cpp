// Behaviour tests for the mmio (RIFF file I/O) stand-in, driven the way DSUtil's CWaveFile
// drives it: write a WAV through CreateChunk/Write/GetInfo/Advance, read it back through
// Descend/Read/Seek and the buffered GetInfo/Advance loop, and from an in-memory resource.
#include "ran_compat.h"
#include <mmsystem.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static WAVEFORMATEX Pcm16Mono()
{
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = 22050;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = 44100;
    return wfx;
}

// Same calls as CWaveFile::Open(WAVEFILE_WRITE) + WriteMMIO + ResetFile + Write + Close.
static bool WriteWav(const std::string& path, const std::vector<BYTE>& samples)
{
    HMMIO h = mmioOpen((LPSTR)path.c_str(), NULL, MMIO_ALLOCBUF | MMIO_READWRITE | MMIO_CREATE);
    if (!h) return false;
    MMCKINFO riff = {}, ck = {}, fact = {};
    riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
    riff.cksize = 0;
    if (mmioCreateChunk(h, &riff, MMIO_CREATERIFF) != 0) return false;
    ck.ckid = mmioFOURCC('f', 'm', 't', ' ');
    ck.cksize = sizeof(PCMWAVEFORMAT);
    if (mmioCreateChunk(h, &ck, 0) != 0) return false;
    WAVEFORMATEX wfx = Pcm16Mono();
    if (mmioWrite(h, (HPSTR)&wfx, sizeof(PCMWAVEFORMAT)) != (LONG)sizeof(PCMWAVEFORMAT)) return false;
    if (mmioAscend(h, &ck, 0) != 0) return false;
    fact.ckid = mmioFOURCC('f', 'a', 'c', 't');
    fact.cksize = 0;
    if (mmioCreateChunk(h, &fact, 0) != 0) return false;
    DWORD factValue = (DWORD)-1;
    if (mmioWrite(h, (HPSTR)&factValue, sizeof(factValue)) != (LONG)sizeof(factValue)) return false;
    if (mmioAscend(h, &fact, 0) != 0) return false;

    ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
    ck.cksize = 0;
    if (mmioCreateChunk(h, &ck, 0) != 0) return false;
    MMIOINFO out = {};
    if (mmioGetInfo(h, &out, 0) != 0) return false;
    for (size_t i = 0; i < samples.size(); ++i) {
        if (out.pchNext == out.pchEndWrite) {
            out.dwFlags |= MMIO_DIRTY;
            if (mmioAdvance(h, &out, MMIO_WRITE) != 0) return false;
        }
        *out.pchNext++ = (char)samples[i];
    }
    out.dwFlags |= MMIO_DIRTY;
    if (mmioSetInfo(h, &out, 0) != 0) return false;
    if (mmioAscend(h, &ck, 0) != 0) return false;
    if (mmioAscend(h, &riff, 0) != 0) return false;
    return mmioClose(h, 0) == 0;
}

// Same calls as CWaveFile::ReadMMIO + ResetFile + Read.
static bool ReadWav(HMMIO h, WAVEFORMATEX* wfxOut, std::vector<BYTE>* data)
{
    MMCKINFO riff = {}, ckIn = {}, ck = {};
    if (mmioDescend(h, &riff, NULL, 0) != 0) return false;
    if (riff.ckid != FOURCC_RIFF || riff.fccType != mmioFOURCC('W', 'A', 'V', 'E')) return false;
    ckIn.ckid = mmioFOURCC('f', 'm', 't', ' ');
    if (mmioDescend(h, &ckIn, &riff, MMIO_FINDCHUNK) != 0) return false;
    if (ckIn.cksize < (LONG)sizeof(PCMWAVEFORMAT)) return false;
    PCMWAVEFORMAT pcm = {};
    if (mmioRead(h, (HPSTR)&pcm, sizeof(pcm)) != (LONG)sizeof(pcm)) return false;
    std::memcpy(wfxOut, &pcm, sizeof(pcm));
    if (mmioAscend(h, &ckIn, 0) != 0) return false;

    if (mmioSeek(h, riff.dwDataOffset + sizeof(FOURCC), SEEK_SET) == -1) return false;
    ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
    if (mmioDescend(h, &ck, &riff, MMIO_FINDCHUNK) != 0) return false;

    MMIOINFO in = {};
    if (mmioGetInfo(h, &in, 0) != 0) return false;
    data->assign(ck.cksize, 0);
    for (DWORD i = 0; i < ck.cksize; ++i) {
        if (in.pchNext == in.pchEndRead) {
            if (mmioAdvance(h, &in, MMIO_READ) != 0) return false;
            if (in.pchNext == in.pchEndRead) return false;
        }
        (*data)[i] = (BYTE)*in.pchNext++;
    }
    return mmioSetInfo(h, &in, 0) == 0;
}

static DWORD Le32(const std::vector<BYTE>& b, size_t at)
{
    return (DWORD)b[at] | ((DWORD)b[at + 1] << 8) | ((DWORD)b[at + 2] << 16) | ((DWORD)b[at + 3] << 24);
}

int main()
{
    char tmpl[] = "/tmp/ran_mmio_test_XXXXXX";
    const std::string dir = ::mkdtemp(tmpl);
    const std::string path = dir + "/tone.wav";

    // Odd length on purpose: RIFF pads the data chunk to an even size.
    std::vector<BYTE> samples(70001);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = (BYTE)(i * 7 + 3);
    CHECK(WriteWav(path, samples));

    // ---- The file on disk has the exact Windows RIFF layout.
    std::vector<BYTE> file;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        int c;
        while ((c = std::fgetc(f)) != EOF) file.push_back((BYTE)c);
        std::fclose(f);
    }
    const size_t expected = 12 + (8 + 16) + (8 + 4) + (8 + samples.size() + 1);
    CHECK(file.size() == expected);
    if (file.size() == expected) {
        CHECK(std::memcmp(&file[0], "RIFF", 4) == 0);
        CHECK(Le32(file, 4) == expected - 8);
        CHECK(std::memcmp(&file[8], "WAVE", 4) == 0);
        CHECK(std::memcmp(&file[12], "fmt ", 4) == 0 && Le32(file, 16) == 16);
        CHECK(std::memcmp(&file[36], "fact", 4) == 0 && Le32(file, 40) == 4);
        CHECK(std::memcmp(&file[48], "data", 4) == 0 && Le32(file, 52) == samples.size());
        CHECK(std::memcmp(&file[56], samples.data(), samples.size()) == 0);
        CHECK(file.back() == 0);
    }

    // ---- Reading it back from disk, through a Windows-style relative path.
    {
        CHECK(chdir(dir.c_str()) == 0);
        CHECK(mmioOpen((LPSTR)"missing.wav", NULL, MMIO_READ) == NULL);
        HMMIO h = mmioOpen((LPSTR)".\\tone.wav", NULL, MMIO_ALLOCBUF | MMIO_READ);
        CHECK(h != NULL);
        WAVEFORMATEX wfx = {};
        std::vector<BYTE> data;
        CHECK(h && ReadWav(h, &wfx, &data));
        CHECK(wfx.nSamplesPerSec == 22050 && wfx.wBitsPerSample == 16 && wfx.nChannels == 1);
        CHECK(data == samples);
        // FINDCHUNK stays inside the parent: no "LIST" chunk in this file.
        MMCKINFO riff = {}, list = {};
        CHECK(mmioSeek(h, 0, SEEK_SET) == 0);
        CHECK(mmioDescend(h, &riff, NULL, 0) == 0);
        list.ckid = mmioFOURCC('L', 'I', 'S', 'T');
        CHECK(mmioDescend(h, &list, &riff, MMIO_FINDCHUNK) == MMIOERR_CHUNKNOTFOUND);
        if (h) mmioClose(h, 0);
    }

    // ---- In-memory resource (FOURCC_MEM), as CWaveFile uses for WAVs embedded in the exe.
    {
        MMIOINFO mem = {};
        mem.fccIOProc = FOURCC_MEM;
        mem.cchBuffer = (LONG)file.size();
        mem.pchBuffer = (char*)file.data();
        HMMIO h = mmioOpen(NULL, &mem, MMIO_ALLOCBUF | MMIO_READ);
        CHECK(h != NULL);
        WAVEFORMATEX wfx = {};
        std::vector<BYTE> data;
        CHECK(h && ReadWav(h, &wfx, &data));
        CHECK(data == samples);
        if (h) mmioClose(h, 0);
    }

    std::remove(path.c_str());
    ::rmdir(dir.c_str());
    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("mmio_test: all checks passed\n");
    return 0;
}

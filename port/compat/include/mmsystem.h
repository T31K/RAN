// Stand-in for <mmsystem.h> (native macOS build): multimedia types used by the sound code and
// the DirectSound/DirectInput SDK headers. timeGetTime lives in win32/kernel.h.
#pragma once
#include "ran_compat.h"

typedef UINT MMRESULT;
typedef DWORD FOURCC;
#ifndef MAKEFOURCC
#define MAKEFOURCC(c0, c1, c2, c3) ((DWORD)(BYTE)(c0) | ((DWORD)(BYTE)(c1) << 8) | ((DWORD)(BYTE)(c2) << 16) | ((DWORD)(BYTE)(c3) << 24))
#endif
#define mmioFOURCC MAKEFOURCC
#define MMSYSERR_NOERROR 0
#define TIMERR_NOERROR   0

#ifndef _WAVEFORMATEX_
#define _WAVEFORMATEX_
#pragma pack(push, 1)
typedef struct tWAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX* LPCWAVEFORMATEX;
#pragma pack(pop)
static_assert(sizeof(WAVEFORMATEX) == 18, "WAVEFORMATEX must match the Windows layout");
#endif

#pragma pack(push, 1)
typedef struct waveformat_tag {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
} WAVEFORMAT, *LPWAVEFORMAT;
typedef struct pcmwaveformat_tag {
    WAVEFORMAT wf;
    WORD       wBitsPerSample;
} PCMWAVEFORMAT, *LPPCMWAVEFORMAT;
#pragma pack(pop)

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 1
#endif

// RIFF/mmio types used by the WAV loader (DSUtil). The functions are in win32/mmio.h below.
typedef void* HMMIO;
typedef struct _MMCKINFO {
    FOURCC ckid;
    DWORD  cksize;
    FOURCC fccType;
    DWORD  dwDataOffset;
    DWORD  dwFlags;
} MMCKINFO, *PMMCKINFO, *LPMMCKINFO;
typedef struct _MMIOINFO {
    DWORD  dwFlags;
    FOURCC fccIOProc;
    void*  pIOProc;
    UINT   wErrorRet;
    void*  htask;
    LONG   cchBuffer;
    char*  pchBuffer;
    char*  pchNext;
    char*  pchEndRead;
    char*  pchEndWrite;
    LONG   lBufOffset;
    LONG   lDiskOffset;
    DWORD  adwInfo[3];
    DWORD  dwReserved1, dwReserved2;
    HMMIO  hmmio;
} MMIOINFO, *PMMIOINFO, *LPMMIOINFO;
typedef const MMIOINFO* LPCMMIOINFO;
#define MMIO_READ       0x00000000
#define MMIO_WRITE      0x00000001
#define MMIO_READWRITE  0x00000002
#define MMIO_ALLOCBUF   0x00010000
#define MMIO_FINDRIFF   0x0020
#define MMIO_FINDCHUNK  0x0010
#define MMIO_CREATERIFF 0x0020
#define MMIO_DIRTY      0x10000000
#define MMIOM_READ      MMIO_READ
#define SEEK_SET_MMIO   0
#define FOURCC_RIFF     mmioFOURCC('R', 'I', 'F', 'F')
#define FOURCC_MEM      mmioFOURCC('M', 'E', 'M', ' ')

#include "win32/mmio.h"

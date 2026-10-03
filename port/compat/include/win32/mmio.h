// mmio (Windows multimedia RIFF file I/O) for the native macOS build. DSUtil's CWaveFile reads
// every game WAV through it, and can write WAVs too. Each handle holds the whole file in memory
// (game WAVs are small), which keeps the buffered GetInfo/Advance/SetInfo interface simple:
// pchNext/pchEndRead point straight into that buffer. Writes go to disk on mmioClose.
// Included from <mmsystem.h>; needs win32/files.h for Windows path resolution.
#pragma once
#include "win32/files.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

typedef char* HPSTR;

#ifndef MMIO_CREATE
#define MMIO_CREATE      0x00001000
#endif
#ifndef MMIO_FINDLIST
#define MMIO_FINDLIST    0x0040
#endif
#ifndef MMIO_CREATELIST
#define MMIO_CREATELIST  0x0040
#endif
#ifndef MMIOERR_BASE
#define MMIOERR_BASE             256
#define MMIOERR_FILENOTFOUND     (MMIOERR_BASE + 1)
#define MMIOERR_CANNOTWRITE      (MMIOERR_BASE + 4)
#define MMIOERR_CANNOTSEEK       (MMIOERR_BASE + 6)
#define MMIOERR_CANNOTEXPAND     (MMIOERR_BASE + 8)
#define MMIOERR_CHUNKNOTFOUND    (MMIOERR_BASE + 9)
#endif
#ifndef FOURCC_LIST
#define FOURCC_LIST mmioFOURCC('L', 'I', 'S', 'T')
#endif

namespace ran_compat {
    struct MmioFile {
        std::vector<char> buf;   // capacity; bytes past `size` are scratch space for buffered writes
        size_t size = 0;         // logical file length
        size_t pos = 0;
        bool writable = false;
        bool dirty = false;
        std::string path;        // empty for FOURCC_MEM handles

        void Put(const void* src, size_t n)
        {
            if (buf.size() < pos + n) buf.resize(pos + n);
            std::memcpy(buf.data() + pos, src, n);
            pos += n;
            size = std::max(size, pos);
            dirty = true;
        }
        bool Get32(size_t at, DWORD* v) const
        {
            if (at + 4 > size) return false;
            const unsigned char* b = (const unsigned char*)buf.data() + at;
            *v = (DWORD)b[0] | ((DWORD)b[1] << 8) | ((DWORD)b[2] << 16) | ((DWORD)b[3] << 24);
            return true;
        }
        void Set32(size_t at, DWORD v)
        {
            const unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
            const size_t saved = pos;
            pos = at;
            Put(b, 4);
            pos = saved;
        }
        // Points the caller's MMIOINFO at the buffer. Writers get at least `grow` bytes of room.
        void Expose(LPMMIOINFO info, size_t grow)
        {
            if (writable && buf.size() < pos + grow) buf.resize(pos + grow);
            info->pchBuffer = buf.data();
            info->cchBuffer = (LONG)buf.size();
            info->pchNext = buf.data() + pos;
            info->pchEndRead = buf.data() + size;
            info->pchEndWrite = writable ? buf.data() + buf.size() : buf.data() + size;
            info->lBufOffset = 0;
            info->lDiskOffset = (LONG)pos;
        }
        // Takes back the position (and, if MMIO_DIRTY, the written bytes) from the caller.
        void Sync(LPMMIOINFO info)
        {
            if (!info->pchNext) return;
            pos = (size_t)(info->pchNext - buf.data());
            if (info->dwFlags & MMIO_DIRTY) {
                size = std::max(size, pos);
                dirty = true;
                info->dwFlags &= ~(DWORD)MMIO_DIRTY;
            }
        }
    };
    constexpr size_t kMmioWriteStep = 64 * 1024;
}

inline HMMIO mmioOpen(LPSTR szFilename, LPMMIOINFO lpmmioinfo, DWORD dwOpenFlags)
{
    auto* m = new ran_compat::MmioFile();
    m->writable = (dwOpenFlags & (MMIO_WRITE | MMIO_READWRITE)) != 0;
    if (lpmmioinfo && lpmmioinfo->fccIOProc == FOURCC_MEM) {
        if (lpmmioinfo->pchBuffer && lpmmioinfo->cchBuffer > 0)
            m->buf.assign(lpmmioinfo->pchBuffer, lpmmioinfo->pchBuffer + lpmmioinfo->cchBuffer);
        m->size = m->buf.size();
        m->writable = false;
        return (HMMIO)m;
    }
    if (!szFilename) { delete m; return NULL; }
    m->path = ran_compat::ResolvePath(szFilename);
    if (dwOpenFlags & MMIO_CREATE) {
        FILE* f = std::fopen(m->path.c_str(), "wb");   // create/truncate now so failures show at open
        if (!f) { delete m; return NULL; }
        std::fclose(f);
        m->writable = true;
        m->dirty = true;
        return (HMMIO)m;
    }
    FILE* f = std::fopen(m->path.c_str(), "rb");
    if (!f) { delete m; return NULL; }
    char chunk[64 * 1024];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) m->buf.insert(m->buf.end(), chunk, chunk + n);
    std::fclose(f);
    m->size = m->buf.size();
    return (HMMIO)m;
}

inline MMRESULT mmioClose(HMMIO hmmio, UINT)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m) return MMIOERR_CANNOTWRITE;
    MMRESULT r = MMSYSERR_NOERROR;
    if (m->writable && m->dirty && !m->path.empty()) {
        FILE* f = std::fopen(m->path.c_str(), "wb");
        if (!f || std::fwrite(m->buf.data(), 1, m->size, f) != m->size) r = MMIOERR_CANNOTWRITE;
        if (f) std::fclose(f);
    }
    delete m;
    return r;
}

inline LONG mmioRead(HMMIO hmmio, HPSTR pch, LONG cch)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m || cch < 0) return -1;
    const size_t avail = m->pos < m->size ? m->size - m->pos : 0;
    const size_t n = std::min(avail, (size_t)cch);
    if (n) std::memcpy(pch, m->buf.data() + m->pos, n);
    m->pos += n;
    return (LONG)n;
}

inline LONG mmioWrite(HMMIO hmmio, const char* pch, LONG cch)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m || !m->writable || cch < 0) return -1;
    m->Put(pch, (size_t)cch);
    return cch;
}

inline LONG mmioSeek(HMMIO hmmio, LONG lOffset, int iOrigin)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m) return -1;
    long long base = iOrigin == SEEK_CUR ? (long long)m->pos : iOrigin == SEEK_END ? (long long)m->size : 0;
    const long long target = base + lOffset;
    if (target < 0) return -1;
    m->pos = (size_t)target;
    return (LONG)target;
}

inline MMRESULT mmioDescend(HMMIO hmmio, LPMMCKINFO lpck, const MMCKINFO* lpckParent, UINT wFlags)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m || !lpck) return MMIOERR_CHUNKNOTFOUND;
    const size_t end = lpckParent ? std::min(m->size, (size_t)lpckParent->dwDataOffset + lpckParent->cksize) : m->size;
    const FOURCC wantId = lpck->ckid, wantType = lpck->fccType;
    size_t at = m->pos;
    while (at + 8 <= end) {
        DWORD id = 0, sz = 0, type = 0;
        m->Get32(at, &id);
        m->Get32(at + 4, &sz);
        const bool isForm = id == FOURCC_RIFF || id == FOURCC_LIST;
        if (isForm) m->Get32(at + 8, &type);
        bool match = true;
        if (wFlags & MMIO_FINDCHUNK)     match = id == wantId;
        else if (wFlags & MMIO_FINDRIFF) match = id == FOURCC_RIFF && type == wantType;
        else if (wFlags & MMIO_FINDLIST) match = id == FOURCC_LIST && type == wantType;
        if (match) {
            lpck->ckid = id;
            lpck->cksize = sz;
            lpck->fccType = isForm ? type : 0;
            lpck->dwDataOffset = (DWORD)(at + 8);
            lpck->dwFlags = 0;
            m->pos = at + 8 + (isForm ? 4 : 0);
            return MMSYSERR_NOERROR;
        }
        at += 8 + (size_t)sz + (sz & 1);   // chunks are word-aligned
    }
    return MMIOERR_CHUNKNOTFOUND;
}

inline MMRESULT mmioAscend(HMMIO hmmio, LPMMCKINFO lpck, UINT)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m || !lpck) return MMIOERR_CANNOTSEEK;
    if (lpck->dwFlags & MMIO_DIRTY) {
        // Written through mmioCreateChunk: the size is wherever writing stopped.
        const DWORD written = m->pos > lpck->dwDataOffset ? (DWORD)(m->pos - lpck->dwDataOffset) : 0;
        if (written != lpck->cksize) {
            lpck->cksize = written;
            m->Set32(lpck->dwDataOffset - 4, written);
        }
        m->pos = (size_t)lpck->dwDataOffset + lpck->cksize;
        if (lpck->cksize & 1) { const char pad = 0; m->Put(&pad, 1); }
        lpck->dwFlags &= ~(DWORD)MMIO_DIRTY;
        return MMSYSERR_NOERROR;
    }
    m->pos = (size_t)lpck->dwDataOffset + lpck->cksize + (lpck->cksize & 1);
    return MMSYSERR_NOERROR;
}

inline MMRESULT mmioCreateChunk(HMMIO hmmio, LPMMCKINFO lpck, UINT wFlags)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m || !m->writable || !lpck) return MMIOERR_CANNOTWRITE;
    if (wFlags & MMIO_CREATERIFF) lpck->ckid = FOURCC_RIFF;
    else if (wFlags & MMIO_CREATELIST) lpck->ckid = FOURCC_LIST;
    const size_t at = m->pos;
    m->Set32(at, lpck->ckid);
    m->Set32(at + 4, lpck->cksize);
    m->pos = at + 8;
    lpck->dwDataOffset = (DWORD)m->pos;
    if (wFlags & (MMIO_CREATERIFF | MMIO_CREATELIST)) {
        m->Set32(m->pos, lpck->fccType);
        m->pos += 4;
    }
    lpck->dwFlags = MMIO_DIRTY;
    return MMSYSERR_NOERROR;
}

inline MMRESULT mmioGetInfo(HMMIO hmmio, LPMMIOINFO lpmmioinfo, UINT)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m || !lpmmioinfo) return MMIOERR_CANNOTSEEK;
    std::memset(lpmmioinfo, 0, sizeof(*lpmmioinfo));
    lpmmioinfo->hmmio = hmmio;
    lpmmioinfo->dwFlags = m->writable ? MMIO_READWRITE : MMIO_READ;
    m->Expose(lpmmioinfo, ran_compat::kMmioWriteStep);
    return MMSYSERR_NOERROR;
}

inline MMRESULT mmioSetInfo(HMMIO hmmio, LPCMMIOINFO lpmmioinfo, UINT)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m || !lpmmioinfo) return MMIOERR_CANNOTSEEK;
    MMIOINFO copy = *lpmmioinfo;
    m->Sync(&copy);
    return MMSYSERR_NOERROR;
}

inline MMRESULT mmioAdvance(HMMIO hmmio, LPMMIOINFO lpmmioinfo, UINT wFlags)
{
    auto* m = (ran_compat::MmioFile*)hmmio;
    if (!m || !lpmmioinfo) return MMIOERR_CANNOTSEEK;
    m->Sync(lpmmioinfo);
    if ((wFlags & MMIO_WRITE) && !m->writable) return MMIOERR_CANNOTWRITE;
    // Reading: the whole file is already exposed, so at the end pchNext stays == pchEndRead
    // (that is how Windows reports EOF too). Writing: hand out another block of room.
    m->Expose(lpmmioinfo, (wFlags & MMIO_WRITE) ? ran_compat::kMmioWriteStep : 0);
    return MMSYSERR_NOERROR;
}

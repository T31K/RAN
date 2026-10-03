// The CryptoAPI subset the game uses (Engine Common/CompByte.cpp, for the saved user ID in
// RANPARAM): MD5 hash -> CryptDeriveKey(CALG_RC4) -> CryptEncrypt/CryptDecrypt.
//
// Key length follows Microsoft's providers: 128-bit RC4 from the first 16 hash bytes (Strong/
// Enhanced provider default, what CryptAcquireContext(NULL provider, PROV_RSA_FULL) gets on
// current Windows). ran_compat::SetCryptRc4KeyBits(40) switches to the Base provider's 40-bit
// key + 11-byte zero salt. TODO(port): confirm against a Windows-generated vector.
#pragma once
#include "win32/kernel.h"
#include <CommonCrypto/CommonDigest.h>
#include <vector>

typedef ULONG_PTR HCRYPTPROV;
typedef ULONG_PTR HCRYPTKEY;
typedef ULONG_PTR HCRYPTHASH;
typedef unsigned int ALG_ID;

#define PROV_RSA_FULL        1
#define CRYPT_VERIFYCONTEXT  0xF0000000
#define CRYPT_EXPORTABLE     0x00000001
#define CRYPT_NO_SALT        0x00000010
#define CALG_MD5             0x00008003
#define CALG_RC4             0x00006801
#define MS_DEF_PROV          "Microsoft Base Cryptographic Provider v1.0"
#define MS_ENHANCED_PROV     "Microsoft Enhanced Cryptographic Provider v1.0"

namespace ran_compat {

    inline int& CryptRc4KeyBits() { static int bits = 128; return bits; }
    inline void SetCryptRc4KeyBits(int bits) { CryptRc4KeyBits() = bits; }

    struct CryptHash { std::vector<BYTE> data; };
    struct CryptKey {
        BYTE s[256];
        int i = 0, j = 0;
        void Init(const BYTE* key, size_t len)
        {
            for (int k = 0; k < 256; ++k) s[k] = (BYTE)k;
            for (int k = 0, jj = 0; k < 256; ++k) {
                jj = (jj + s[k] + key[k % len]) & 0xff;
                const BYTE t = s[k]; s[k] = s[jj]; s[jj] = t;
            }
            i = j = 0;
        }
        void Apply(BYTE* buf, size_t len)   // RC4: encryption and decryption are the same
        {
            for (size_t n = 0; n < len; ++n) {
                i = (i + 1) & 0xff;
                j = (j + s[i]) & 0xff;
                const BYTE t = s[i]; s[i] = s[j]; s[j] = t;
                buf[n] ^= s[(s[i] + s[j]) & 0xff];
            }
        }
    };
}

inline BOOL CryptAcquireContext(HCRYPTPROV* prov, const char*, const char*, DWORD, DWORD)
{
    if (prov) *prov = 1;
    return TRUE;
}
#define CryptAcquireContextA CryptAcquireContext
inline BOOL CryptReleaseContext(HCRYPTPROV, DWORD) { return TRUE; }

inline BOOL CryptCreateHash(HCRYPTPROV, ALG_ID alg, HCRYPTKEY, DWORD, HCRYPTHASH* hash)
{
    if (alg != CALG_MD5 || !hash) return FALSE;
    *hash = (HCRYPTHASH) new ran_compat::CryptHash();
    return TRUE;
}
inline BOOL CryptHashData(HCRYPTHASH hash, const BYTE* data, DWORD len, DWORD)
{
    if (!hash) return FALSE;
    auto* h = (ran_compat::CryptHash*)hash;
    h->data.insert(h->data.end(), data, data + len);
    return TRUE;
}
inline BOOL CryptDestroyHash(HCRYPTHASH hash) { delete (ran_compat::CryptHash*)hash; return TRUE; }

inline BOOL CryptDeriveKey(HCRYPTPROV, ALG_ID alg, HCRYPTHASH hash, DWORD flags, HCRYPTKEY* key)
{
    if (alg != CALG_RC4 || !hash || !key) return FALSE;
    auto* h = (ran_compat::CryptHash*)hash;
    BYTE digest[CC_MD5_DIGEST_LENGTH];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_MD5(h->data.data(), (CC_LONG)h->data.size(), digest);   // MD5 is what the game asked for
#pragma clang diagnostic pop
    int bits = (int)(flags >> 16);
    if (bits == 0) bits = ran_compat::CryptRc4KeyBits();
    BYTE material[16] = {};
    const size_t keyBytes = (size_t)bits / 8;
    std::memcpy(material, digest, keyBytes < 16 ? keyBytes : 16);
    // Base provider: 40-bit keys carry an 11-byte zero salt (already zeroed) unless CRYPT_NO_SALT.
    const size_t len = (keyBytes < 16 && !(flags & CRYPT_NO_SALT)) ? 16 : keyBytes;
    auto* k = new ran_compat::CryptKey();
    k->Init(material, len);
    *key = (HCRYPTKEY)k;
    return TRUE;
}
inline BOOL CryptDestroyKey(HCRYPTKEY key) { delete (ran_compat::CryptKey*)key; return TRUE; }

inline BOOL CryptEncrypt(HCRYPTKEY key, HCRYPTHASH, BOOL, DWORD, BYTE* data, DWORD* len, DWORD bufLen)
{
    if (!key || !len || (data && *len > bufLen)) return FALSE;
    if (data) ((ran_compat::CryptKey*)key)->Apply(data, *len);
    return TRUE;
}
inline BOOL CryptDecrypt(HCRYPTKEY key, HCRYPTHASH, BOOL, DWORD, BYTE* data, DWORD* len)
{
    if (!key || !len) return FALSE;
    if (data) ((ran_compat::CryptKey*)key)->Apply(data, *len);
    return TRUE;
}

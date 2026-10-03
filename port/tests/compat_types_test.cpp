// Sizes must match the 32-bit Windows build, because these types appear in
// pack(1) packets and serialized data files.
#include "win32_types.h"

static_assert(sizeof(BYTE) == 1);
static_assert(sizeof(WORD) == 2);
static_assert(sizeof(DWORD) == 4);
static_assert(sizeof(LONG) == 4);
static_assert(sizeof(ULONG) == 4);
static_assert(sizeof(BOOL) == 4);
static_assert(sizeof(INT) == 4);
static_assert(sizeof(UINT) == 4);
static_assert(sizeof(LONGLONG) == 8);
static_assert(sizeof(ULONGLONG) == 8);
static_assert(sizeof(__time64_t) == 8);
static_assert(sizeof(HRESULT) == 4);
static_assert(sizeof(WCHAR) == 2);
static_assert(SUCCEEDED(S_OK) && !SUCCEEDED(E_FAIL));
int main() { return 0; }

// Sizes must match the 32-bit Windows build, because these types appear in pack(1) packets
// and serialized data files.
#include "win32_types.h"

static_assert(sizeof(BYTE) == 1, "BYTE");
static_assert(sizeof(WORD) == 2, "WORD");
static_assert(sizeof(DWORD) == 4, "DWORD");
static_assert(sizeof(LONG) == 4, "LONG");
static_assert(sizeof(ULONG) == 4, "ULONG");
static_assert(sizeof(BOOL) == 4, "BOOL");
static_assert(sizeof(INT) == 4, "INT");
static_assert(sizeof(UINT) == 4, "UINT");
static_assert(sizeof(LONGLONG) == 8, "LONGLONG");
static_assert(sizeof(ULONGLONG) == 8, "ULONGLONG");
static_assert(sizeof(__time64_t) == 8, "__time64_t");
static_assert(sizeof(HRESULT) == 4, "HRESULT");
static_assert(sizeof(WCHAR) == 2, "WCHAR");
static_assert(SUCCEEDED(S_OK) && !SUCCEEDED(E_FAIL), "SUCCEEDED");
int main() { return 0; }

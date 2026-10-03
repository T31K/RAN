// Prints "name size" for every network-message struct in struct_list.inc.
// Built two ways:
//   Windows x86 / MSVC (CI, .github/workflows/build.yml) -> golden msg_sizes_win32.txt
//   macOS / clang     (port/scripts/check-struct-sizes.sh) -> compared against the golden file
// Packets go over the wire as raw struct bytes, so every size must match exactly.
#include "../../../[Lib]__RanClient/framework.h"
#include "../../../Dependency/NetGlobal/s_NetGlobal.h"
#include "../../../[Lib]__RanClient/Sources/G-Logic/GLMsg/GLContrlMsg.h"
#include <cstdio>

#define RAN_SIZE(T) std::printf("%s %u\n", #T, (unsigned)sizeof(T));

int main()
{
#include "struct_list.inc"
    return 0;
}

// ODBC base types that unixODBC expects windows.h to provide once ALREADY_HAVE_WINDOWS_TYPE is
// set (see ran_compat.h). Sizes match Windows: SDWORD/UDWORD are 32-bit.
#pragma once
#include "win32/extra_types.h"

#ifndef SQL_API
#define SQL_API
#endif
typedef signed char     SCHAR;
typedef SCHAR           SQLSCHAR;
typedef int32_t         SDWORD;
typedef uint32_t        UDWORD;
typedef int16_t         SWORD;
typedef uint16_t        UWORD;
typedef int32_t         SLONG;
typedef int16_t         SSHORT;
typedef uint16_t        USHORT;
typedef double          SDOUBLE;
typedef double          LDOUBLE;
typedef float           SFLOAT;
typedef void*           PTR;
typedef int16_t         RETCODE;
typedef void*           SQLHWND;

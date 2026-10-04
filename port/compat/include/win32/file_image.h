// Structs the game reads from and writes to its data files as raw bytes, whose Windows x86
// image differs from the native one: texture pointers kept in materials are 4 bytes on disk
// (8 natively, with alignment padding), and a few members carry a vtable pointer. The game's
// streams (basestream/CSerialFile/CSerialMemory/CByteStream) get typed ReadBuffer/WriteBuffer
// overloads in the native build that call ReadImage/WriteImage below: types that declare
// their Windows image (Win32Image<T> specialisations next to the struct definitions) are
// converted element by element, every other type is read and written unchanged.
// The list of affected types comes from port/scripts/gen_file_struct_sizes.py (gate P1.6,
// docs/port/file-structs.md).
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace ran_compat {

template <class...> struct VoidT { typedef void type; };

// Default: the bytes on disk are the bytes in memory - unless T declares its image inside its
// own definition (RAN_WIN32_IMAGE_POINTERS_MEMBER, for types nested in non-public sections).
template <class T, class = void> struct Win32Image
{
    static const bool kConverts = false;
};
template <class T> struct Win32Image<T, typename VoidT<typename T::RanWin32Image>::type> : T::RanWin32Image {};

// Members up to the pointer run are laid out identically (4-byte aligned); the run of
// `count` pointers is 4 bytes per pointer on disk and is read back as null (the game
// recreates the objects after loading); the members after the run are identical again.
inline void ReadPointerRun(const BYTE* src, BYTE* dst, size_t win32PtrOffset, size_t nativeAfter, size_t count,
                           size_t win32Size)
{
    const size_t win32After = win32PtrOffset + 4 * count;
    std::memcpy(dst, src, win32PtrOffset);
    std::memset(dst + win32PtrOffset, 0, nativeAfter - win32PtrOffset);
    std::memcpy(dst + nativeAfter, src + win32After, win32Size - win32After);
}

inline void WritePointerRun(const BYTE* src, BYTE* dst, size_t win32PtrOffset, size_t nativeAfter, size_t count,
                            size_t win32Size)
{
    const size_t win32After = win32PtrOffset + 4 * count;
    std::memcpy(dst, src, win32PtrOffset);
    std::memset(dst + win32PtrOffset, 0, 4 * count);   // pointer values mean nothing on disk
    std::memcpy(dst + win32After, src + nativeAfter, win32Size - win32After);
}

// MSVC x86 std::string as raw bytes (24): a 16-byte union of the inline text and a heap
// pointer, then size and capacity. Files written by the Windows tools only hold usable text
// when it fits inline (capacity 15); a heap pointer in a file means nothing, on Windows too.
inline std::string ReadMsvcString(const BYTE* p)
{
    DWORD size = 0, capacity = 0;
    std::memcpy(&size, p + 16, 4);
    std::memcpy(&capacity, p + 20, 4);
    if (capacity < 16 && size <= capacity) return std::string((const char*)p, size);
    return std::string();
}

inline void WriteMsvcString(const std::string& s, BYTE* p)
{
    std::memset(p, 0, 24);
    const DWORD size = (DWORD)(s.size() < 15 ? s.size() : 15), capacity = 15;
    std::memcpy(p, s.data(), size);
    std::memcpy(p + 16, &size, 4);
    std::memcpy(p + 20, &capacity, 4);
}

template <class Stream, class T> BOOL ReadImage(Stream& s, T* p, DWORD size, std::false_type)
{
    return s.ReadBuffer((void*)p, size);
}

template <class Stream, class T> BOOL ReadImage(Stream& s, T* p, DWORD size, std::true_type)
{
    if (!p || size % sizeof(T)) return s.ReadBuffer((void*)p, size);   // not whole elements: as is
    const size_t n = size / sizeof(T);
    std::vector<BYTE> image(Win32Image<T>::kSize * n);
    const BOOL ok = s.ReadBuffer((void*)image.data(), (DWORD)image.size());
    for (size_t i = 0; i < n; ++i) Win32Image<T>::Read(image.data() + i * Win32Image<T>::kSize, p[i]);
    return ok;
}

template <class Stream, class T> BOOL ReadImage(Stream& s, T* p, DWORD size)
{
    return ReadImage(s, p, size, std::integral_constant<bool, Win32Image<T>::kConverts>());
}

template <class Stream, class T> BOOL WriteImage(Stream& s, const T* p, DWORD size, std::false_type)
{
    return s.WriteBuffer((const void*)p, size);
}

template <class Stream, class T> BOOL WriteImage(Stream& s, const T* p, DWORD size, std::true_type)
{
    if (!p || size % sizeof(T)) return s.WriteBuffer((const void*)p, size);
    const size_t n = size / sizeof(T);
    std::vector<BYTE> image(Win32Image<T>::kSize * n, 0);
    for (size_t i = 0; i < n; ++i) Win32Image<T>::Write(p[i], image.data() + i * Win32Image<T>::kSize);
    return s.WriteBuffer((const void*)image.data(), (DWORD)image.size());
}

template <class Stream, class T> BOOL WriteImage(Stream& s, const T* p, DWORD size)
{
    return WriteImage(s, p, size, std::integral_constant<bool, Win32Image<T>::kConverts>());
}

} // namespace ran_compat

// Declares the Windows image of struct T: members before `firstPointer` as on Windows, the
// pointers from `firstPointer` up to (not including) `afterPointers` stored as 4-byte slots
// starting at Windows offset `win32PtrOffset`, the rest as on Windows; `win32Size` bytes.
// Place after the struct; inside it (types nested in a non-public section) use the _MEMBER form.
#define RAN_WIN32_IMAGE_POINTERS(T, firstPointer, afterPointers, win32PtrOffset, win32Size)                          \
    template <> struct ran_compat::Win32Image<T>                                                                      \
        RAN_WIN32_IMAGE_POINTERS_BODY(T, firstPointer, afterPointers, win32PtrOffset, win32Size);
#define RAN_WIN32_IMAGE_POINTERS_MEMBER(T, firstPointer, afterPointers, win32PtrOffset, win32Size)                   \
    struct RanWin32Image RAN_WIN32_IMAGE_POINTERS_BODY(T, firstPointer, afterPointers, win32PtrOffset, win32Size);
#define RAN_WIN32_IMAGE_POINTERS_BODY(T, firstPointer, afterPointers, win32PtrOffset, win32Size)                     \
    {                                                                                                                  \
        static const bool kConverts = true;                                                                            \
        static const size_t kSize = (win32Size);                                                                       \
        static size_t Count() { return (offsetof(T, afterPointers) - offsetof(T, firstPointer)) / sizeof(void*); }     \
        static void Read(const BYTE* s, T& d)                                                                          \
        {                                                                                                              \
            ran_compat::ReadPointerRun(s, (BYTE*)&d, (win32PtrOffset), offsetof(T, afterPointers), Count(), kSize);    \
        }                                                                                                              \
        static void Write(const T& d, BYTE* s)                                                                         \
        {                                                                                                              \
            ran_compat::WritePointerRun((const BYTE*)&d, s, (win32PtrOffset), offsetof(T, afterPointers), Count(), kSize); \
        }                                                                                                              \
    };

// The typed overloads every game stream class declares in the native build.
#define RAN_STREAM_IMAGE_OVERLOADS                                                                                     \
    template <class T> BOOL ReadBuffer(T* p, DWORD size) { return ran_compat::ReadImage(*this, p, size); }            \
    template <class T> BOOL WriteBuffer(const T* p, DWORD size) { return ran_compat::WriteImage(*this, p, size); }

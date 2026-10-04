// DirectX .x file reader behind the native DirectXFileCreate (legacy IDirectXFile*, dxfile.h)
// and D3DXFileCreate (ID3DXFile*, d3dx9xof.h), both implemented in xfile.cpp.
//
// Reads "xof 0302"/"xof 0303" files in binary and text encoding, float size 0032 or 0064
// (compressed tzip/bzip files are rejected). Templates come from the file itself and from
// RegisterTemplates blobs (D3DRM_XTEMPLATES is binary .x data holding only templates).
//
// Each data object gets its data blob exactly as DirectX hands it out from GetData/Lock: the
// template's members in order, packed (no padding), WORD 2 / DWORD 4 / FLOAT 4 / DOUBLE 8 bytes
// (0064 files' doubles become 4-byte floats in FLOAT members), STRING members as a `char*` to a
// NUL-terminated string owned by the object (8 bytes on arm64), nested structs and arrays inline.
// Binary integer/float lists may span several members; they are consumed value by value.
//
// The COM objects are the API for game code. The parser below is exposed for tests and tools.
// Tested by port/tests/xfile_test.cpp.
#pragma once
#include "ran_compat.h"
#include <d3d9.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace ran_xfile {

// Primitive member types; the values are the binary-format token numbers.
enum Prim : int {
    PRIM_NONE = 0,      // member typed with another template
    PRIM_WORD = 40, PRIM_DWORD = 41, PRIM_FLOAT = 42, PRIM_DOUBLE = 43, PRIM_CHAR = 44,
    PRIM_UCHAR = 45, PRIM_SWORD = 46, PRIM_SDWORD = 47, PRIM_VOID = 48, PRIM_LPSTR = 49,
    PRIM_UNICODE = 50, PRIM_CSTRING = 51,
};

struct Template
{
    struct Dim { uint32_t fixed = 0; std::string ref; };   // `ref` empty -> fixed size
    struct Member
    {
        std::string name;
        int prim = PRIM_NONE;
        std::string typeName;        // template name when prim == PRIM_NONE
        bool array = false;
        std::vector<Dim> dims;       // one per [..], arrays only
    };
    enum Openness { CLOSED, OPEN, RESTRICTED };

    std::string name;
    GUID id = {};
    std::vector<Member> members;
    Openness openness = CLOSED;
    struct Allowed { std::string name; GUID id = {}; bool hasId = false; };
    std::vector<Allowed> allowed;    // RESTRICTED only
};

// Registered templates, looked up by name (later definitions replace earlier ones).
class TemplateSet
{
public:
    void Add(std::shared_ptr<const Template> t);
    void AddAll(const TemplateSet& other);
    const Template* Find(const std::string& name) const;
    std::shared_ptr<const Template> FindShared(const std::string& name) const;
    size_t Size() const { return m_list.size(); }
    const std::vector<std::shared_ptr<const Template>>& List() const { return m_list; }
private:
    std::vector<std::shared_ptr<const Template>> m_list;
};

struct Object;

struct Child
{
    std::shared_ptr<Object> object;  // a nested data object (not set for references)
    bool reference = false;          // `{ name }` / `{ <guid> }`
    std::string refName;
    GUID refId = {};
    bool refHasId = false;
    Object* target = nullptr;        // the referenced object (owned by the same Document)
};

struct Object
{
    std::string name;                // empty when unnamed
    GUID id = {};
    bool hasId = false;
    std::shared_ptr<const Template> tmpl;
    std::vector<BYTE> data;          // the GetData/Lock blob
    struct Span { std::string name; size_t offset = 0, size = 0; };
    std::vector<Span> members;       // the template's top-level members inside `data`
    std::vector<Child> children;     // file order
    std::deque<std::string> strings;         // STRING/CSTRING members point in here
    std::deque<std::u16string> wstrings;     // UNICODE members point in here
};

struct Document
{
    std::vector<std::shared_ptr<Object>> objects;   // top-level data objects, file order
    TemplateSet templates;                          // templates the file itself defines
};

enum class Error {
    Ok, BadFileType, BadVersion, BadFloatSize, Compressed, Parse, NoTemplate, BadArraySize,
    BadReference, OutOfMemory,
};

// Parses one .x file. `known` supplies registered templates (the file's own definitions win);
// the file's templates end up in out.templates. References are resolved across the whole
// document (any depth, forward too); one that resolves to nothing fails with BadReference.
Error Parse(const void* data, size_t size, const TemplateSet& known, Document& out);

const char* ErrorName(Error e);

} // namespace ran_xfile

// DirectX .x file reader and the two COM APIs built on it: the legacy DirectX File API
// (DirectXFileCreate, dxfile.h) and D3DX's (D3DXFileCreate, d3dx9xof.h). See xfile.h.
//
// The whole file is parsed when the enum object is created; the COM objects are views on the
// parsed Document (shared, so a data object outlives its enum and file object safely).
//
// Format notes (checked against the shipped client data and D3DRM_XTEMPLATES):
//   - 16-byte header "xof 03xxbin 0032" ("txt " for text, "0064" = double-precision floats).
//   - Binary tokens are a WORD code; NAME/STRING carry a DWORD length + bytes (no NUL; a STRING
//     is followed by a separate SEMICOLON/COMMA token), INTEGER a DWORD, GUID 16 bytes,
//     INTEGER_LIST/FLOAT_LIST a DWORD count + values. One list may cover several members (a
//     Mesh's nVertices and nFaces lists run into the face indices), so lists are handed to the
//     data parser one value at a time.
//   - Text: `,` and `;` separate values; the parser takes values by the template's layout and
//     skips separators. `//` and `#` start comments.
#include "xfile.h"
#include <d3dx9.h>
#include <dxfile.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>

namespace ran_xfile {

// --- templates --------------------------------------------------------------------------------

void TemplateSet::Add(std::shared_ptr<const Template> t)
{
    for (auto& e : m_list)
        if (e->name == t->name) { e = std::move(t); return; }
    m_list.push_back(std::move(t));
}

void TemplateSet::AddAll(const TemplateSet& other)
{
    for (const auto& t : other.m_list) Add(t);
}

std::shared_ptr<const Template> TemplateSet::FindShared(const std::string& name) const
{
    for (const auto& t : m_list)
        if (t->name == name) return t;
    return nullptr;
}

const Template* TemplateSet::Find(const std::string& name) const
{
    return FindShared(name).get();
}

const char* ErrorName(Error e)
{
    switch (e) {
    case Error::Ok: return "ok";
    case Error::BadFileType: return "bad file type";
    case Error::BadVersion: return "bad file version";
    case Error::BadFloatSize: return "bad float size";
    case Error::Compressed: return "compressed file (not supported)";
    case Error::Parse: return "parse error";
    case Error::NoTemplate: return "unknown template";
    case Error::BadArraySize: return "bad array size";
    case Error::BadReference: return "unresolved data reference";
    case Error::OutOfMemory: return "out of memory";
    }
    return "?";
}

namespace {

// Token codes: the binary format's numbers, plus T_FLOAT for one float value.
enum Tok : int {
    T_ERROR = -1, T_END = 0,
    T_NAME = 1, T_STRING = 2, T_INTEGER = 3, T_GUID = 5, T_INTEGER_LIST = 6, T_FLOAT_LIST = 7,
    T_OBRACE = 10, T_CBRACE = 11, T_OPAREN = 12, T_CPAREN = 13, T_OBRACKET = 14, T_CBRACKET = 15,
    T_OANGLE = 16, T_CANGLE = 17, T_DOT = 18, T_COMMA = 19, T_SEMICOLON = 20,
    T_TEMPLATE = 31,
    T_WORD = 40, T_DWORD = 41, T_FLOAT_KW = 42, T_DOUBLE = 43, T_CHAR = 44, T_UCHAR = 45,
    T_SWORD = 46, T_SDWORD = 47, T_VOID = 48, T_LPSTR = 49, T_UNICODE = 50, T_CSTRING = 51,
    T_ARRAY = 52,
    T_FLOAT = 100,
};

struct Token
{
    int type = T_END;
    std::string text;      // NAME, STRING
    int64_t ival = 0;      // INTEGER
    double fval = 0;       // FLOAT (and text INTEGER, which may fill a float member)
    GUID guid = {};
};

const int kMaxDepth = 64;   // nested objects / nested template members

bool ParseGuidText(const char* s, size_t n, GUID& g)
{
    while (n && std::isspace((unsigned char)*s)) { ++s; --n; }
    while (n && std::isspace((unsigned char)s[n - 1])) --n;
    if (n != 36) return false;
    char buf[37];
    std::memcpy(buf, s, 36);
    buf[36] = 0;
    unsigned a, b, c, d[8];
    int used = 0;
    if (std::sscanf(buf, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x%n", &a, &b, &c, &d[0], &d[1], &d[2],
                    &d[3], &d[4], &d[5], &d[6], &d[7], &used) != 11 || used != 36)
        return false;
    g.Data1 = a; g.Data2 = (WORD)b; g.Data3 = (WORD)c;
    for (int i = 0; i < 8; ++i) g.Data4[i] = (BYTE)d[i];
    return true;
}

// Turns binary or text .x data (after the header) into tokens. Binary integer/float lists come
// out one value per token.
class Lexer
{
public:
    Lexer(const BYTE* p, const BYTE* end, bool binary, bool doubles)
        : m_p(p), m_end(end), m_binary(binary), m_doubles(doubles) {}

    const Token& Peek()
    {
        if (!m_havePeek) { m_peek = Read(); m_havePeek = true; }
        return m_peek;
    }
    Token Next()
    {
        if (m_havePeek) { m_havePeek = false; return std::move(m_peek); }
        return Read();
    }
    bool Binary() const { return m_binary; }
    // Upper bound for the values still to come (array-size sanity check).
    size_t Remaining() const { return (size_t)(m_end - m_p) + m_listLeft + 1; }

private:
    size_t Left() const { return (size_t)(m_end - m_p); }
    uint32_t Get32() { uint32_t v; std::memcpy(&v, m_p, 4); m_p += 4; return v; }

    Token Read() { return m_binary ? ReadBinary() : ReadText(); }

    Token ReadBinary()
    {
        Token t;
        for (;;) {
            if (m_listLeft) {
                --m_listLeft;
                if (m_listInts) {
                    t.type = T_INTEGER; t.ival = Get32();
                } else if (m_doubles) {
                    double d; std::memcpy(&d, m_p, 8); m_p += 8;
                    t.type = T_FLOAT; t.fval = d;
                } else {
                    float f; std::memcpy(&f, m_p, 4); m_p += 4;
                    t.type = T_FLOAT; t.fval = f;
                }
                return t;
            }
            if (Left() < 2) { t.type = Left() ? T_ERROR : T_END; return t; }
            WORD code; std::memcpy(&code, m_p, 2); m_p += 2;
            switch (code) {
            case T_NAME: case T_STRING: {
                if (Left() < 4) { t.type = T_ERROR; return t; }
                uint32_t n = Get32();
                if (n > Left()) { t.type = T_ERROR; return t; }
                t.text.assign((const char*)m_p, n);
                m_p += n;
                t.type = code;
                return t;
            }
            case T_INTEGER:
                if (Left() < 4) { t.type = T_ERROR; return t; }
                t.type = T_INTEGER; t.ival = Get32();
                return t;
            case T_GUID:
                if (Left() < 16) { t.type = T_ERROR; return t; }
                std::memcpy(&t.guid, m_p, 16); m_p += 16;   // little-endian GUID layout
                t.type = T_GUID;
                return t;
            case T_INTEGER_LIST: case T_FLOAT_LIST: {
                if (Left() < 4) { t.type = T_ERROR; return t; }
                uint32_t n = Get32();
                size_t each = (code == T_INTEGER_LIST || !m_doubles) ? 4 : 8;
                if (n > Left() / each) { t.type = T_ERROR; return t; }
                m_listLeft = n;
                m_listInts = code == T_INTEGER_LIST;
                continue;   // an empty list yields nothing
            }
            default:
                if ((code >= T_OBRACE && code <= T_SEMICOLON) || code == T_TEMPLATE ||
                    (code >= T_WORD && code <= T_ARRAY)) {
                    t.type = code;
                    return t;
                }
                t.type = T_ERROR;
                return t;
            }
        }
    }

    static bool NameChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c >= 0x80; }

    Token ReadText()
    {
        Token t;
        for (;;) {
            while (m_p < m_end && (std::isspace(*m_p) || *m_p == 0)) ++m_p;
            if (m_p < m_end && (*m_p == '#' || (*m_p == '/' && m_p + 1 < m_end && m_p[1] == '/'))) {
                while (m_p < m_end && *m_p != '\n') ++m_p;
                continue;
            }
            break;
        }
        if (m_p >= m_end) { t.type = T_END; return t; }
        const unsigned char c = *m_p;
        switch (c) {
        case '{': ++m_p; t.type = T_OBRACE; return t;
        case '}': ++m_p; t.type = T_CBRACE; return t;
        case '(': ++m_p; t.type = T_OPAREN; return t;
        case ')': ++m_p; t.type = T_CPAREN; return t;
        case '[': ++m_p; t.type = T_OBRACKET; return t;
        case ']': ++m_p; t.type = T_CBRACKET; return t;
        case ',': ++m_p; t.type = T_COMMA; return t;
        case ';': ++m_p; t.type = T_SEMICOLON; return t;
        case '>': ++m_p; t.type = T_CANGLE; return t;
        case '<': {
            const BYTE* close = m_p + 1;
            while (close < m_end && *close != '>' && close - m_p < 64) ++close;
            if (close < m_end && *close == '>' && ParseGuidText((const char*)m_p + 1, close - m_p - 1, t.guid)) {
                m_p = close + 1;
                t.type = T_GUID;
            } else {
                ++m_p;
                t.type = T_OANGLE;
            }
            return t;
        }
        case '"': {
            const BYTE* s = ++m_p;
            while (m_p < m_end && *m_p != '"') ++m_p;
            if (m_p >= m_end) { t.type = T_ERROR; return t; }
            t.text.assign((const char*)s, m_p - s);
            ++m_p;
            t.type = T_STRING;
            return t;
        }
        default: break;
        }
        const bool more = m_p + 1 < m_end;
        const bool number = std::isdigit(c) ||
            ((c == '-' || c == '+') && more && (std::isdigit(m_p[1]) || m_p[1] == '.')) ||
            (c == '.' && more && std::isdigit(m_p[1]));
        if (number) return ReadNumber();
        if (c == '.') { ++m_p; t.type = T_DOT; return t; }
        if (std::isalpha(c) || c == '_' || c >= 0x80) {
            const BYTE* s = m_p;
            while (m_p < m_end && NameChar(*m_p)) ++m_p;
            t.text.assign((const char*)s, m_p - s);
            t.type = T_NAME;
            return t;
        }
        t.type = T_ERROR;
        return t;
    }

    Token ReadNumber()
    {
        Token t;
        const BYTE* s = m_p;
        bool isFloat = false;
        if (*m_p == '-' || *m_p == '+') ++m_p;
        while (m_p < m_end) {
            const unsigned char c = *m_p;
            if (std::isdigit(c)) { ++m_p; continue; }
            if (c == '.' || c == 'e' || c == 'E') { isFloat = true; ++m_p; continue; }
            if ((c == '-' || c == '+') && (m_p[-1] == 'e' || m_p[-1] == 'E')) { ++m_p; continue; }
            break;
        }
        std::string num((const char*)s, m_p - s);
        // MSVC's printf spells NaN/infinity "1.#QNAN0", "-1.#IND00", "1.#INF00".
        if (m_p < m_end && *m_p == '#') {
            isFloat = true;
            while (m_p < m_end && (std::isalnum(*m_p) || *m_p == '#')) ++m_p;
        }
        if (isFloat) {
            t.type = T_FLOAT;
            t.fval = std::strtod(num.c_str(), nullptr);
        } else {
            t.type = T_INTEGER;
            t.ival = std::strtoll(num.c_str(), nullptr, 10);
            t.fval = (double)t.ival;
        }
        return t;
    }

    const BYTE* m_p;
    const BYTE* m_end;
    bool m_binary, m_doubles;
    uint32_t m_listLeft = 0;
    bool m_listInts = false;
    bool m_havePeek = false;
    Token m_peek;
};

template <class T> void Put(std::vector<BYTE>& v, T x)
{
    const size_t n = v.size();
    v.resize(n + sizeof(T));
    std::memcpy(&v[n], &x, sizeof(T));
}

class Parser
{
public:
    Parser(Lexer& lx, const TemplateSet& known, Document& doc) : m_lx(lx), m_known(known), m_doc(doc) {}

    Error Run()
    {
        for (;;) {
            SkipSeparators();
            const Token& t = m_lx.Peek();
            if (t.type == T_END) break;
            if (Keyword(t) == T_TEMPLATE) {
                m_lx.Next();
                Error e = ParseTemplate();
                if (e != Error::Ok) return e;
            } else if (t.type == T_NAME) {
                std::string type = m_lx.Next().text;
                std::shared_ptr<Object> o;
                Error e = ParseObject(type, o, 0);
                if (e != Error::Ok) return e;
                m_doc.objects.push_back(std::move(o));
            } else {
                return Error::Parse;
            }
        }
        return ResolveReferences();
    }

private:
    // Keyword code of a token: binary keyword tokens as they are, text names by spelling.
    int Keyword(const Token& t) const
    {
        if (t.type == T_TEMPLATE || (t.type >= T_WORD && t.type <= T_ARRAY)) return t.type;
        if (t.type != T_NAME || m_lx.Binary()) return 0;
        static const struct { const char* name; int code; } kWords[] = {
            { "template", T_TEMPLATE }, { "WORD", T_WORD }, { "DWORD", T_DWORD }, { "FLOAT", T_FLOAT_KW },
            { "DOUBLE", T_DOUBLE }, { "CHAR", T_CHAR }, { "UCHAR", T_UCHAR }, { "BYTE", T_UCHAR },
            { "SWORD", T_SWORD }, { "SDWORD", T_SDWORD }, { "VOID", T_VOID }, { "STRING", T_LPSTR },
            { "LPSTR", T_LPSTR }, { "UNICODE", T_UNICODE }, { "CSTRING", T_CSTRING }, { "array", T_ARRAY },
        };
        for (const auto& w : kWords)
            if (strcasecmp(t.text.c_str(), w.name) == 0) return w.code;
        return 0;
    }

    void SkipSeparators()
    {
        for (;;) {
            int t = m_lx.Peek().type;
            if (t != T_COMMA && t != T_SEMICOLON) return;
            m_lx.Next();
        }
    }

    std::shared_ptr<const Template> FindTemplate(const std::string& name) const
    {
        std::shared_ptr<const Template> t = m_doc.templates.FindShared(name);
        return t ? t : m_known.FindShared(name);
    }

    // template <name> { <guid> members... [restriction] }
    Error ParseTemplate()
    {
        Token name = m_lx.Next();
        if (name.type != T_NAME) return Error::Parse;
        auto t = std::make_shared<Template>();
        t->name = name.text;
        if (m_lx.Next().type != T_OBRACE) return Error::Parse;
        if (m_lx.Peek().type == T_GUID) t->id = m_lx.Next().guid;
        for (;;) {
            Token tok = m_lx.Next();
            if (tok.type == T_CBRACE) break;
            if (tok.type == T_SEMICOLON || tok.type == T_COMMA) continue;
            if (tok.type == T_OBRACKET) {
                Error e = ParseRestriction(*t);
                if (e != Error::Ok) return e;
                continue;
            }
            Template::Member m;
            int kw = Keyword(tok);
            if (kw == T_ARRAY) {
                m.array = true;
                tok = m_lx.Next();
                kw = Keyword(tok);
            }
            if (kw >= T_WORD && kw <= T_CSTRING) m.prim = kw;
            else if (tok.type == T_NAME) m.typeName = tok.text;
            else return Error::Parse;
            if (m_lx.Peek().type == T_NAME) m.name = m_lx.Next().text;
            if (m.array) {
                if (m_lx.Peek().type != T_OBRACKET) return Error::Parse;
                while (m_lx.Peek().type == T_OBRACKET) {
                    m_lx.Next();
                    Token d = m_lx.Next();
                    Template::Dim dim;
                    if (d.type == T_INTEGER && d.ival >= 0) dim.fixed = (uint32_t)d.ival;
                    else if (d.type == T_NAME) dim.ref = d.text;
                    else return Error::Parse;
                    if (m_lx.Next().type != T_CBRACKET) return Error::Parse;
                    m.dims.push_back(dim);
                }
            }
            t->members.push_back(std::move(m));
        }
        m_doc.templates.Add(t);
        return Error::Ok;
    }

    // After '[': "..." (open) or a list of allowed template names, each with an optional GUID.
    Error ParseRestriction(Template& t)
    {
        if (m_lx.Peek().type == T_DOT) {
            while (m_lx.Peek().type == T_DOT) m_lx.Next();
            if (m_lx.Next().type != T_CBRACKET) return Error::Parse;
            t.openness = Template::OPEN;
            return Error::Ok;
        }
        t.openness = Template::RESTRICTED;
        for (;;) {
            Token r = m_lx.Next();
            if (r.type == T_CBRACKET) return Error::Ok;
            if (r.type == T_COMMA) continue;
            if (r.type == T_NAME) {
                Template::Allowed a;
                a.name = r.text;
                t.allowed.push_back(a);
            } else if (r.type == T_GUID) {
                if (t.allowed.empty() || t.allowed.back().hasId) t.allowed.push_back(Template::Allowed());
                t.allowed.back().id = r.guid;
                t.allowed.back().hasId = true;
            } else {
                return Error::Parse;
            }
        }
    }

    Error ReadInt(int64_t& v)
    {
        SkipSeparators();
        Token t = m_lx.Next();
        if (t.type != T_INTEGER) return Error::Parse;
        v = t.ival;
        return Error::Ok;
    }

    Error ReadFloat(double& v)
    {
        SkipSeparators();
        Token t = m_lx.Next();
        if (t.type == T_FLOAT || (t.type == T_INTEGER && !m_lx.Binary())) { v = t.fval; return Error::Ok; }
        return Error::Parse;
    }

    Error ReadString(std::string& s)
    {
        SkipSeparators();
        Token t = m_lx.Next();
        if (t.type != T_STRING) return Error::Parse;
        s = std::move(t.text);
        return Error::Ok;
    }

    // One value of member `m` (one array element) appended to o.data. `iv` gets integer values
    // (array sizes may refer to them).
    Error ParseValue(const Template::Member& m, Object& o, int64_t& iv, int depth)
    {
        Error e = Error::Ok;
        double d = 0;
        std::string s;
        switch (m.prim) {
        case PRIM_WORD: case PRIM_SWORD:
            if ((e = ReadInt(iv)) == Error::Ok) Put(o.data, (WORD)iv);
            return e;
        case PRIM_DWORD: case PRIM_SDWORD:
            if ((e = ReadInt(iv)) == Error::Ok) Put(o.data, (DWORD)iv);
            return e;
        case PRIM_CHAR: case PRIM_UCHAR:
            if ((e = ReadInt(iv)) == Error::Ok) Put(o.data, (BYTE)iv);
            return e;
        case PRIM_FLOAT:
            if ((e = ReadFloat(d)) == Error::Ok) Put(o.data, (float)d);
            return e;
        case PRIM_DOUBLE:
            if ((e = ReadFloat(d)) == Error::Ok) Put(o.data, d);
            return e;
        case PRIM_LPSTR: case PRIM_CSTRING:
            if ((e = ReadString(s)) != Error::Ok) return e;
            o.strings.push_back(std::move(s));
            Put(o.data, o.strings.back().c_str());
            return Error::Ok;
        case PRIM_UNICODE: {
            if ((e = ReadString(s)) != Error::Ok) return e;
            std::u16string w;
            for (unsigned char c : s) w.push_back((char16_t)c);
            o.wstrings.push_back(std::move(w));
            Put(o.data, o.wstrings.back().c_str());
            return Error::Ok;
        }
        case PRIM_VOID:
            return Error::Ok;
        default: {
            std::shared_ptr<const Template> sub = FindTemplate(m.typeName);
            if (!sub) return Error::NoTemplate;
            return ParseMembers(*sub, o, false, depth + 1);
        }
        }
    }

    // The members of `t`, in order, into o.data. `top`: record member spans (GetData by name).
    Error ParseMembers(const Template& t, Object& o, bool top, int depth)
    {
        if (depth > kMaxDepth) return Error::Parse;
        std::vector<std::pair<const std::string*, int64_t>> scalars;   // for array sizes
        for (const Template::Member& m : t.members) {
            uint64_t count = 1;
            for (const Template::Dim& dim : m.dims) {
                uint64_t n = dim.fixed;
                if (!dim.ref.empty()) {
                    bool found = false;
                    for (auto it = scalars.rbegin(); it != scalars.rend(); ++it)
                        if (*it->first == dim.ref) { n = (uint64_t)(uint32_t)it->second; found = true; break; }
                    if (!found) return Error::BadArraySize;
                }
                count *= n;
                if (count > m_lx.Remaining()) return Error::BadArraySize;
            }
            const size_t start = o.data.size();
            int64_t iv = 0;
            for (uint64_t i = 0; i < count; ++i) {
                Error e = ParseValue(m, o, iv, depth);
                if (e != Error::Ok) return e;
            }
            if (!m.array && m.prim != PRIM_NONE) scalars.emplace_back(&m.name, iv);
            if (top) {
                Object::Span span;
                span.name = m.name;
                span.offset = start;
                span.size = o.data.size() - start;
                o.members.push_back(std::move(span));
            }
        }
        return Error::Ok;
    }

    // <type> [name] { [<guid>] members... children... }   (the type name is already read)
    Error ParseObject(const std::string& type, std::shared_ptr<Object>& out, int depth)
    {
        if (depth > kMaxDepth) return Error::Parse;
        std::shared_ptr<const Template> tmpl = FindTemplate(type);
        if (!tmpl) return Error::NoTemplate;
        auto o = std::make_shared<Object>();
        o->tmpl = tmpl;
        if (m_lx.Peek().type == T_NAME) o->name = m_lx.Next().text;
        if (m_lx.Next().type != T_OBRACE) return Error::Parse;
        if (m_lx.Peek().type == T_GUID) { o->id = m_lx.Next().guid; o->hasId = true; }
        Error e = ParseMembers(*tmpl, *o, true, depth);
        if (e != Error::Ok) return e;
        for (;;) {
            SkipSeparators();
            Token t = m_lx.Next();
            if (t.type == T_CBRACE) break;
            Child c;
            if (t.type == T_OBRACE) {
                c.reference = true;
                if (m_lx.Peek().type == T_NAME) c.refName = m_lx.Next().text;
                if (m_lx.Peek().type == T_GUID) { c.refId = m_lx.Next().guid; c.refHasId = true; }
                SkipSeparators();
                if (m_lx.Next().type != T_CBRACE) return Error::Parse;
                if (c.refName.empty() && !c.refHasId) return Error::Parse;
            } else if (t.type == T_NAME) {
                e = ParseObject(t.text, c.object, depth + 1);
                if (e != Error::Ok) return e;
            } else {
                return Error::Parse;
            }
            o->children.push_back(std::move(c));
        }
        out = std::move(o);
        return Error::Ok;
    }

    void Index(Object& o, std::map<std::string, Object*>& byName, std::vector<Object*>& all)
    {
        all.push_back(&o);
        if (!o.name.empty()) byName.emplace(o.name, &o);   // first definition wins
        for (Child& c : o.children)
            if (c.object) Index(*c.object, byName, all);
    }

    Error ResolveReferences()
    {
        std::map<std::string, Object*> byName;
        std::vector<Object*> all;
        for (auto& o : m_doc.objects) Index(*o, byName, all);
        for (Object* o : all) {
            for (Child& c : o->children) {
                if (!c.reference) continue;
                if (!c.refName.empty()) {
                    auto it = byName.find(c.refName);
                    if (it != byName.end()) c.target = it->second;
                } else {
                    for (Object* x : all)
                        if (x->hasId && std::memcmp(&x->id, &c.refId, sizeof(GUID)) == 0) { c.target = x; break; }
                }
                if (!c.target) return Error::BadReference;
            }
        }
        return Error::Ok;
    }

    Lexer& m_lx;
    const TemplateSet& m_known;
    Document& m_doc;
};

} // namespace

Error Parse(const void* data, size_t size, const TemplateSet& known, Document& out)
{
    const BYTE* p = (const BYTE*)data;
    if (!p || size < 16 || std::memcmp(p, "xof ", 4) != 0) return Error::BadFileType;
    if (std::memcmp(p + 4, "03", 2) != 0 || !std::isdigit(p[6]) || !std::isdigit(p[7])) return Error::BadVersion;
    bool binary;
    if (std::memcmp(p + 8, "bin ", 4) == 0) binary = true;
    else if (std::memcmp(p + 8, "txt ", 4) == 0) binary = false;
    else if (std::memcmp(p + 8, "tzip", 4) == 0 || std::memcmp(p + 8, "bzip", 4) == 0) return Error::Compressed;
    else return Error::BadFileType;
    bool doubles;
    if (std::memcmp(p + 12, "0032", 4) == 0) doubles = false;
    else if (std::memcmp(p + 12, "0064", 4) == 0) doubles = true;
    else return Error::BadFloatSize;
    try {
        Lexer lx(p + 16, p + size, binary, doubles);
        Parser parser(lx, known, out);
        return parser.Run();
    } catch (const std::bad_alloc&) {
        return Error::OutOfMemory;
    } catch (...) {
        return Error::Parse;
    }
}

} // namespace ran_xfile

// --- COM objects ------------------------------------------------------------------------------

using namespace ran_xfile;

namespace {

template <class I> class ComBase : public I
{
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG n = --m_refs;
        if (n == 0) delete this;
        return n;
    }
protected:
    virtual ~ComBase() {}
    std::atomic<ULONG> m_refs{ 1 };
};

bool SameGuid(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

bool ReadWholeFile(const char* path, std::vector<BYTE>& out)
{
    FILE* f = ran_compat::fopen_resolved(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = n >= 0;
    if (ok) {
        out.resize((size_t)n);
        ok = n == 0 || std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    }
    std::fclose(f);
    return ok;
}

// Parse with every exception turned into an error code (COM methods must not throw).
Error ParseDocument(const void* data, size_t size, const TemplateSet& known, std::shared_ptr<Document>& out)
{
    try {
        auto doc = std::make_shared<Document>();
        Error e = Parse(data, size, known, *doc);
        if (e == Error::Ok) out = std::move(doc);
        return e;
    } catch (...) {
        return Error::OutOfMemory;
    }
}

// Depth-first search over every data object of a document.
Object* FindObject(const std::vector<std::shared_ptr<Object>>& list, const char* name, const GUID* id)
{
    for (const auto& o : list) {
        if (name ? (!o->name.empty() && o->name == name) : (o->hasId && SameGuid(o->id, *id))) return o.get();
        std::vector<std::shared_ptr<Object>> kids;
        for (const Child& c : o->children)
            if (c.object) kids.push_back(c.object);
        if (Object* found = FindObject(kids, name, id)) return found;
    }
    return nullptr;
}

// --- legacy DirectX File API (dxfile.h) -------------------------------------------------------

HRESULT LegacyError(Error e)
{
    switch (e) {
    case Error::Ok: return S_OK;
    case Error::BadFileType: return DXFILEERR_BADFILETYPE;
    case Error::BadVersion: return DXFILEERR_BADFILEVERSION;
    case Error::BadFloatSize: return DXFILEERR_BADFILEFLOATSIZE;
    case Error::Compressed: return DXFILEERR_BADFILECOMPRESSIONTYPE;
    case Error::Parse: return DXFILEERR_PARSEERROR;
    case Error::NoTemplate: return DXFILEERR_NOTEMPLATE;
    case Error::BadArraySize: return DXFILEERR_BADARRAYSIZE;
    case Error::BadReference: return DXFILEERR_BADDATAREFERENCE;
    case Error::OutOfMemory: return E_OUTOFMEMORY;
    }
    return E_FAIL;
}

HRESULT LegacyGetName(const std::string& name, LPSTR buf, LPDWORD size)
{
    if (!size) return DXFILEERR_BADVALUE;
    const DWORD n = name.empty() ? 0 : (DWORD)name.size() + 1;   // unnamed: 0
    if (buf) {
        if (*size < n) return DXFILEERR_BADVALUE;
        if (n) std::memcpy(buf, name.c_str(), n);
        else if (*size) buf[0] = 0;
    }
    *size = n;
    return S_OK;
}

class DXData final : public ComBase<IDirectXFileData>
{
public:
    DXData(std::shared_ptr<Document> doc, Object* o) : m_doc(std::move(doc)), m_obj(o) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override
    {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirectXFileObject || riid == IID_IDirectXFileData) {
            AddRef();
            *out = static_cast<IDirectXFileData*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetName(LPSTR buf, LPDWORD size) override { return LegacyGetName(m_obj->name, buf, size); }
    HRESULT STDMETHODCALLTYPE GetId(LPGUID id) override
    {
        if (!id) return DXFILEERR_BADVALUE;
        *id = m_obj->id;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetData(LPCSTR member, DWORD* size, void** data) override
    {
        if (!size || !data) return DXFILEERR_BADVALUE;
        BYTE* base = m_obj->data.data();
        if (!member) {
            *size = (DWORD)m_obj->data.size();
            *data = base;
            return S_OK;
        }
        for (const Object::Span& s : m_obj->members) {
            if (s.name == member) {
                *size = (DWORD)s.size;
                *data = base + s.offset;
                return S_OK;
            }
        }
        return DXFILEERR_BADDATAREFERENCE;
    }
    HRESULT STDMETHODCALLTYPE GetType(const GUID** type) override
    {
        if (!type) return DXFILEERR_BADVALUE;
        *type = &m_obj->tmpl->id;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNextObject(LPDIRECTXFILEOBJECT* out) override;
    HRESULT STDMETHODCALLTYPE AddDataObject(LPDIRECTXFILEDATA) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AddDataReference(LPCSTR, const GUID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AddBinaryObject(LPCSTR, const GUID*, LPCSTR, LPVOID, DWORD) override { return E_NOTIMPL; }

private:
    std::shared_ptr<Document> m_doc;
    Object* m_obj;
    size_t m_next = 0;
};

class DXRef final : public ComBase<IDirectXFileDataReference>
{
public:
    DXRef(std::shared_ptr<Document> doc, const Child* c) : m_doc(std::move(doc)), m_child(c) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override
    {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirectXFileObject || riid == IID_IDirectXFileDataReference) {
            AddRef();
            *out = static_cast<IDirectXFileDataReference*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetName(LPSTR buf, LPDWORD size) override { return LegacyGetName(m_child->refName, buf, size); }
    HRESULT STDMETHODCALLTYPE GetId(LPGUID id) override
    {
        if (!id) return DXFILEERR_BADVALUE;
        *id = m_child->refHasId ? m_child->refId : GUID();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Resolve(LPDIRECTXFILEDATA* out) override
    {
        if (!out) return DXFILEERR_BADVALUE;
        *out = nullptr;
        if (!m_child->target) return DXFILEERR_BADDATAREFERENCE;
        *out = new (std::nothrow) DXData(m_doc, m_child->target);
        return *out ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::shared_ptr<Document> m_doc;
    const Child* m_child;
};

HRESULT DXData::GetNextObject(LPDIRECTXFILEOBJECT* out)
{
    if (!out) return DXFILEERR_BADVALUE;
    *out = nullptr;
    if (m_next >= m_obj->children.size()) return DXFILEERR_NOMOREOBJECTS;
    const Child& c = m_obj->children[m_next++];
    if (c.reference) *out = new (std::nothrow) DXRef(m_doc, &c);
    else *out = new (std::nothrow) DXData(m_doc, c.object.get());
    return *out ? S_OK : E_OUTOFMEMORY;
}

class DXEnum final : public ComBase<IDirectXFileEnumObject>
{
public:
    explicit DXEnum(std::shared_ptr<Document> doc) : m_doc(std::move(doc)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override
    {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirectXFileEnumObject) {
            AddRef();
            *out = static_cast<IDirectXFileEnumObject*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetNextDataObject(LPDIRECTXFILEDATA* out) override
    {
        if (!out) return DXFILEERR_BADVALUE;
        *out = nullptr;
        if (m_next >= m_doc->objects.size()) return DXFILEERR_NOMOREOBJECTS;
        *out = new (std::nothrow) DXData(m_doc, m_doc->objects[m_next++].get());
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetDataObjectById(REFGUID id, LPDIRECTXFILEDATA* out) override
    {
        if (!out) return DXFILEERR_BADVALUE;
        *out = nullptr;
        Object* o = FindObject(m_doc->objects, nullptr, &id);
        if (!o) return DXFILEERR_NOTFOUND;
        *out = new (std::nothrow) DXData(m_doc, o);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetDataObjectByName(LPCSTR name, LPDIRECTXFILEDATA* out) override
    {
        if (!out || !name) return DXFILEERR_BADVALUE;
        *out = nullptr;
        Object* o = FindObject(m_doc->objects, name, nullptr);
        if (!o) return DXFILEERR_NOTFOUND;
        *out = new (std::nothrow) DXData(m_doc, o);
        return *out ? S_OK : E_OUTOFMEMORY;
    }

private:
    std::shared_ptr<Document> m_doc;
    size_t m_next = 0;
};

class DXFile final : public ComBase<IDirectXFile>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override
    {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirectXFile) {
            AddRef();
            *out = static_cast<IDirectXFile*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE CreateEnumObject(LPVOID src, DXFILELOADOPTIONS options, LPDIRECTXFILEENUMOBJECT* out) override
    {
        if (!out) return DXFILEERR_BADVALUE;
        *out = nullptr;
        if (!src) return DXFILEERR_BADVALUE;
        std::vector<BYTE> bytes;
        const void* data;
        size_t size;
        switch (options) {
        case DXFILELOAD_FROMFILE:
            try {
                if (!ReadWholeFile((const char*)src, bytes)) return DXFILEERR_FILENOTFOUND;
            } catch (...) { return E_OUTOFMEMORY; }
            data = bytes.data(); size = bytes.size();
            break;
        case DXFILELOAD_FROMMEMORY: {
            const DXFILELOADMEMORY* mem = (const DXFILELOADMEMORY*)src;
            data = mem->lpMemory; size = mem->dSize;
            break;
        }
        case DXFILELOAD_FROMRESOURCE: return DXFILEERR_RESOURCENOTFOUND;
        default: return DXFILEERR_BADVALUE;
        }
        std::shared_ptr<Document> doc;
        HRESULT hr = LegacyError(ParseDocument(data, size, m_templates, doc));
        if (FAILED(hr)) return hr;
        try { m_templates.AddAll(doc->templates); } catch (...) { return E_OUTOFMEMORY; }   // legacy: file templates stay registered
        *out = new (std::nothrow) DXEnum(std::move(doc));
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateSaveObject(LPCSTR, DXFILEFORMAT, LPDIRECTXFILESAVEOBJECT* out) override
    {
        if (out) *out = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE RegisterTemplates(LPVOID data, DWORD size) override
    {
        if (!data) return DXFILEERR_BADVALUE;
        std::shared_ptr<Document> doc;
        HRESULT hr = LegacyError(ParseDocument(data, size, m_templates, doc));
        if (FAILED(hr)) return hr;
        try { m_templates.AddAll(doc->templates); } catch (...) { return E_OUTOFMEMORY; }
        return S_OK;
    }

private:
    TemplateSet m_templates;
};

// --- D3DX file API (d3dx9xof.h) ---------------------------------------------------------------

HRESULT D3dxError(Error e)
{
    switch (e) {
    case Error::Ok: return S_OK;
    case Error::BadFileType: return D3DXFERR_BADFILETYPE;
    case Error::BadVersion: return D3DXFERR_BADFILEVERSION;
    case Error::BadFloatSize: return D3DXFERR_BADFILEFLOATSIZE;
    case Error::Compressed: return D3DXFERR_BADFILETYPE;
    case Error::Parse: return D3DXFERR_PARSEERROR;
    case Error::NoTemplate: return D3DXFERR_PARSEERROR;
    case Error::BadArraySize: return D3DXFERR_BADARRAYSIZE;
    case Error::BadReference: return D3DXFERR_BADDATAREFERENCE;
    case Error::OutOfMemory: return E_OUTOFMEMORY;
    }
    return E_FAIL;
}

// Lets RegisterEnumTemplates recognise our own enum objects.
const GUID IID_RanXFileEnum = { 0x7a1e3c52, 0x9b0d, 0x4e61, { 0x8f, 0x2a, 0x51, 0x6c, 0x0e, 0x93, 0x7d, 0x14 } };

class XFile;
class XEnum;

class XData final : public ComBase<ID3DXFileData>
{
public:
    XData(XEnum* en, Object* o, bool reference);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override
    {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ID3DXFileData) {
            AddRef();
            *out = static_cast<ID3DXFileData*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetEnum(ID3DXFileEnumObject** out) override;
    HRESULT STDMETHODCALLTYPE GetName(LPSTR buf, SIZE_T* size) override
    {
        if (!size) return D3DXFERR_BADVALUE;
        const SIZE_T n = m_obj->name.size() + 1;   // unnamed: "" (size 1)
        if (buf) {
            if (*size < n) return D3DXFERR_BADVALUE;
            std::memcpy(buf, m_obj->name.c_str(), n);
        }
        *size = n;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetId(LPGUID id) override
    {
        if (!id) return E_POINTER;
        *id = m_obj->id;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Lock(SIZE_T* size, LPCVOID* data) override
    {
        if (!size || !data) return E_POINTER;
        *size = m_obj->data.size();
        *data = m_obj->data.data();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetType(GUID* type) override
    {
        if (!type) return E_POINTER;
        *type = m_obj->tmpl->id;
        return S_OK;
    }
    BOOL STDMETHODCALLTYPE IsReference() override { return m_reference ? TRUE : FALSE; }
    HRESULT STDMETHODCALLTYPE GetChildren(SIZE_T* n) override
    {
        if (!n) return E_POINTER;
        *n = m_obj->children.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetChild(SIZE_T i, ID3DXFileData** out) override;

private:
    ~XData() override;
    XEnum* m_enum;     // holds the document
    Object* m_obj;     // for a reference: the referenced object
    bool m_reference;
};

class XEnum final : public ComBase<ID3DXFileEnumObject>
{
public:
    XEnum(XFile* file, std::shared_ptr<Document> doc);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override
    {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ID3DXFileEnumObject) {
            AddRef();
            *out = static_cast<ID3DXFileEnumObject*>(this);
            return S_OK;
        }
        if (riid == IID_RanXFileEnum) {   // not reference counted: internal use only
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetFile(ID3DXFile** out) override;
    HRESULT STDMETHODCALLTYPE GetChildren(SIZE_T* n) override
    {
        if (!n) return E_POINTER;
        *n = m_doc->objects.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetChild(SIZE_T i, ID3DXFileData** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (i >= m_doc->objects.size()) return E_INVALIDARG;
        *out = new (std::nothrow) XData(this, m_doc->objects[i].get(), false);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetDataObjectById(REFGUID id, ID3DXFileData** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        Object* o = FindObject(m_doc->objects, nullptr, &id);
        if (!o) return D3DXFERR_NOTFOUND;
        *out = new (std::nothrow) XData(this, o, false);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetDataObjectByName(LPCSTR name, ID3DXFileData** out) override
    {
        if (!out || !name) return E_POINTER;
        *out = nullptr;
        Object* o = FindObject(m_doc->objects, name, nullptr);
        if (!o) return D3DXFERR_NOTFOUND;
        *out = new (std::nothrow) XData(this, o, false);
        return *out ? S_OK : E_OUTOFMEMORY;
    }

    const Document& Doc() const { return *m_doc; }

private:
    ~XEnum() override;
    XFile* m_file;
    std::shared_ptr<Document> m_doc;
};

class XFile final : public ComBase<ID3DXFile>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override
    {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ID3DXFile) {
            AddRef();
            *out = static_cast<ID3DXFile*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE CreateEnumObject(LPCVOID src, D3DXF_FILELOADOPTIONS options, ID3DXFileEnumObject** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!src) return E_POINTER;
        std::vector<BYTE> bytes;
        const void* data;
        size_t size;
        switch (options) {
        case D3DXF_FILELOAD_FROMFILE:
        case D3DXF_FILELOAD_FROMWFILE:
            try {
                std::string path;
                if (options == D3DXF_FILELOAD_FROMFILE) {
                    path = (const char*)src;
                } else {
                    for (const WCHAR* w = (const WCHAR*)src; *w; ++w) path.push_back(*w < 0x80 ? (char)*w : '?');
                }
                if (!ReadWholeFile(path.c_str(), bytes)) return D3DXFERR_FILENOTFOUND;
            } catch (...) { return E_OUTOFMEMORY; }
            data = bytes.data(); size = bytes.size();
            break;
        case D3DXF_FILELOAD_FROMMEMORY: {
            const D3DXF_FILELOADMEMORY* mem = (const D3DXF_FILELOADMEMORY*)src;
            data = mem->lpMemory; size = mem->dSize;
            break;
        }
        case D3DXF_FILELOAD_FROMRESOURCE: return D3DXFERR_RESOURCENOTFOUND;
        default: return E_INVALIDARG;
        }
        std::shared_ptr<Document> doc;
        HRESULT hr = D3dxError(ParseDocument(data, size, m_templates, doc));
        if (FAILED(hr)) return hr;
        *out = new (std::nothrow) XEnum(this, std::move(doc));   // file templates stay with the enum
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateSaveObject(LPCVOID, D3DXF_FILESAVEOPTIONS, D3DXF_FILEFORMAT, ID3DXFileSaveObject** out) override
    {
        if (out) *out = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE RegisterTemplates(LPCVOID data, SIZE_T size) override
    {
        if (!data) return E_POINTER;
        std::shared_ptr<Document> doc;
        HRESULT hr = D3dxError(ParseDocument(data, size, m_templates, doc));
        if (FAILED(hr)) return hr;
        try { m_templates.AddAll(doc->templates); } catch (...) { return E_OUTOFMEMORY; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RegisterEnumTemplates(ID3DXFileEnumObject* e) override
    {
        if (!e) return E_POINTER;
        void* impl = nullptr;
        if (FAILED(e->QueryInterface(IID_RanXFileEnum, &impl)) || !impl) return E_INVALIDARG;
        try { m_templates.AddAll(static_cast<XEnum*>(impl)->Doc().templates); } catch (...) { return E_OUTOFMEMORY; }
        return S_OK;
    }

private:
    TemplateSet m_templates;
};

XEnum::XEnum(XFile* file, std::shared_ptr<Document> doc) : m_file(file), m_doc(std::move(doc)) { m_file->AddRef(); }
XEnum::~XEnum() { m_file->Release(); }

HRESULT XEnum::GetFile(ID3DXFile** out)
{
    if (!out) return E_POINTER;
    m_file->AddRef();
    *out = m_file;
    return S_OK;
}

XData::XData(XEnum* en, Object* o, bool reference) : m_enum(en), m_obj(o), m_reference(reference) { m_enum->AddRef(); }
XData::~XData() { m_enum->Release(); }

HRESULT XData::GetEnum(ID3DXFileEnumObject** out)
{
    if (!out) return E_POINTER;
    m_enum->AddRef();
    *out = m_enum;
    return S_OK;
}

HRESULT XData::GetChild(SIZE_T i, ID3DXFileData** out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (i >= m_obj->children.size()) return E_INVALIDARG;
    const Child& c = m_obj->children[i];
    if (c.reference && !c.target) return D3DXFERR_BADDATAREFERENCE;
    // A reference child shows the referenced object (name, type, data, children), flagged.
    *out = new (std::nothrow) XData(m_enum, c.reference ? c.target : c.object.get(), c.reference);
    return *out ? S_OK : E_OUTOFMEMORY;
}

} // namespace

extern "C" {

HRESULT WINAPI DirectXFileCreate(LPDIRECTXFILE* out)
{
    if (!out) return DXFILEERR_BADVALUE;
    *out = new (std::nothrow) DXFile();
    return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WINAPI D3DXFileCreate(ID3DXFile** out)
{
    if (!out) return E_POINTER;
    *out = new (std::nothrow) XFile();
    return *out ? S_OK : E_OUTOFMEMORY;
}

} // extern "C"

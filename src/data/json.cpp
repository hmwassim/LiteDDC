#include "json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    const Json kNullJson;

    // Recursion-depth guard: settings/layout files are normally written by
    // LiteZones itself, but a hand-edited or corrupted file with deeply
    // nested arrays/objects could otherwise blow the stack via the
    // recursive-descent parser below. 200 levels is far beyond anything a
    // real config file needs.
    constexpr int kMaxNestingDepth = 200;

    struct ParseContext
    {
        const wchar_t* pos = nullptr;
        bool failed = false;
        int depth = 0;
    };

    // RAII depth tracker: increments on construction, decrements on
    // destruction (so every early-return path in ParseObject/ParseArray
    // still unwinds the counter correctly).
    struct DepthGuard
    {
        ParseContext& ctx;
        explicit DepthGuard(ParseContext& c) : ctx(c)
        {
            ++ctx.depth;
        }
        ~DepthGuard()
        {
            --ctx.depth;
        }
        bool TooDeep() const
        {
            return ctx.depth > kMaxNestingDepth;
        }
    };

    void SkipWhitespace(ParseContext& ctx)
    {
        while (*ctx.pos == L' ' || *ctx.pos == L'\t' || *ctx.pos == L'\r' || *ctx.pos == L'\n')
        {
            ++ctx.pos;
        }
    }

    bool ParseHex4(ParseContext& ctx, unsigned& value)
    {
        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const wchar_t c = *ctx.pos;
            unsigned digit;
            if (c >= L'0' && c <= L'9')
            {
                digit = static_cast<unsigned>(c - L'0');
            }
            else if (c >= L'a' && c <= L'f')
            {
                digit = static_cast<unsigned>(c - L'a' + 10);
            }
            else if (c >= L'A' && c <= L'F')
            {
                digit = static_cast<unsigned>(c - L'A' + 10);
            }
            else
            {
                return false;
            }
            value = (value << 4) | digit;
            ++ctx.pos;
        }
        return true;
    }

    bool ParseString(ParseContext& ctx, std::wstring& out)
    {
        if (*ctx.pos != L'"')
        {
            return false;
        }
        ++ctx.pos;

        out.clear();
        while (true)
        {
            const wchar_t c = *ctx.pos;
            if (c == L'\0')
            {
                return false;
            }
            if (c == L'"')
            {
                ++ctx.pos;
                return true;
            }
            if (c == L'\\')
            {
                ++ctx.pos;
                const wchar_t esc = *ctx.pos;
                switch (esc)
                {
                case L'"':
                case L'\\':
                case L'/':
                    out.push_back(esc);
                    break;
                case L'b':
                    out.push_back(L'\b');
                    break;
                case L'f':
                    out.push_back(L'\f');
                    break;
                case L'n':
                    out.push_back(L'\n');
                    break;
                case L'r':
                    out.push_back(L'\r');
                    break;
                case L't':
                    out.push_back(L'\t');
                    break;
                case L'u':
                {
                    ++ctx.pos;
                    unsigned code = 0;
                    if (!ParseHex4(ctx, code))
                    {
                        return false;
                    }
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        // High surrogate: expect a low surrogate right after.
                        if (ctx.pos[0] == L'\\' && ctx.pos[1] == L'u')
                        {
                            ctx.pos += 2;
                            unsigned low = 0;
                            if (!ParseHex4(ctx, low) || low < 0xDC00 || low > 0xDFFF)
                            {
                                return false;
                            }
                            out.push_back(static_cast<wchar_t>(code));
                            out.push_back(static_cast<wchar_t>(low));
                        }
                        else
                        {
                            return false;
                        }
                    }
                    else
                    {
                        out.push_back(static_cast<wchar_t>(code));
                    }
                    continue;
                }
                default:
                    return false;
                }
                ++ctx.pos;
            }
            else
            {
                out.push_back(c);
                ++ctx.pos;
            }
        }
    }

    Json ParseValue(ParseContext& ctx);

    bool ParseArray(ParseContext& ctx, Json& out)
    {
        DepthGuard guard(ctx);
        if (guard.TooDeep())
        {
            return false;
        }

        ++ctx.pos;
        SkipWhitespace(ctx);
        if (*ctx.pos == L']')
        {
            ++ctx.pos;
            return true;
        }
        while (true)
        {
            const Json item = ParseValue(ctx);
            if (ctx.failed)
            {
                return false;
            }
            out.Push(item);
            SkipWhitespace(ctx);
            if (*ctx.pos == L',')
            {
                ++ctx.pos;
                SkipWhitespace(ctx);
            }
            else if (*ctx.pos == L']')
            {
                ++ctx.pos;
                return true;
            }
            else
            {
                return false;
            }
        }
    }

    bool ParseObject(ParseContext& ctx, Json& out)
    {
        DepthGuard guard(ctx);
        if (guard.TooDeep())
        {
            return false;
        }

        ++ctx.pos;
        SkipWhitespace(ctx);
        if (*ctx.pos == L'}')
        {
            ++ctx.pos;
            return true;
        }
        while (true)
        {
            SkipWhitespace(ctx);
            std::wstring key;
            if (!ParseString(ctx, key))
            {
                return false;
            }
            SkipWhitespace(ctx);
            if (*ctx.pos != L':')
            {
                return false;
            }
            ++ctx.pos;
            SkipWhitespace(ctx);
            const Json value = ParseValue(ctx);
            if (ctx.failed)
            {
                return false;
            }
            out.Set(key, value);
            SkipWhitespace(ctx);
            if (*ctx.pos == L',')
            {
                ++ctx.pos;
                SkipWhitespace(ctx);
            }
            else if (*ctx.pos == L'}')
            {
                ++ctx.pos;
                return true;
            }
            else
            {
                return false;
            }
        }
    }

    Json ParseValue(ParseContext& ctx)
    {
        SkipWhitespace(ctx);
        if (ctx.failed)
        {
            return Json::MakeNull();
        }

        const wchar_t c = *ctx.pos;
        if (c == L'{')
        {
            Json obj = Json::MakeObject();
            if (!ParseObject(ctx, obj))
            {
                ctx.failed = true;
            }
            return obj;
        }
        if (c == L'[')
        {
            Json arr = Json::MakeArray();
            if (!ParseArray(ctx, arr))
            {
                ctx.failed = true;
            }
            return arr;
        }
        if (c == L'"')
        {
            std::wstring value;
            if (!ParseString(ctx, value))
            {
                ctx.failed = true;
                return Json::MakeNull();
            }
            return Json::MakeString(value);
        }
        if (c == L't')
        {
            if (ctx.pos[1] == L'r' && ctx.pos[2] == L'u' && ctx.pos[3] == L'e')
            {
                ctx.pos += 4;
                return Json::MakeBool(true);
            }
            ctx.failed = true;
            return Json::MakeNull();
        }
        if (c == L'f')
        {
            if (ctx.pos[1] == L'a' && ctx.pos[2] == L'l' && ctx.pos[3] == L's' && ctx.pos[4] == L'e')
            {
                ctx.pos += 5;
                return Json::MakeBool(false);
            }
            ctx.failed = true;
            return Json::MakeNull();
        }
        if (c == L'n')
        {
            if (ctx.pos[1] == L'u' && ctx.pos[2] == L'l' && ctx.pos[3] == L'l')
            {
                ctx.pos += 4;
                return Json::MakeNull();
            }
            ctx.failed = true;
            return Json::MakeNull();
        }
        if (c == L'-' || (c >= L'0' && c <= L'9'))
        {
            const wchar_t* start = ctx.pos;
            if (c == L'-')
            {
                ++ctx.pos;
            }
            while (*ctx.pos >= L'0' && *ctx.pos <= L'9')
            {
                ++ctx.pos;
            }
            if (*ctx.pos == L'.')
            {
                ++ctx.pos;
                while (*ctx.pos >= L'0' && *ctx.pos <= L'9')
                {
                    ++ctx.pos;
                }
            }
            if (*ctx.pos == L'e' || *ctx.pos == L'E')
            {
                ++ctx.pos;
                if (*ctx.pos == L'+' || *ctx.pos == L'-')
                {
                    ++ctx.pos;
                }
                while (*ctx.pos >= L'0' && *ctx.pos <= L'9')
                {
                    ++ctx.pos;
                }
            }

            std::wstring token(start, ctx.pos);
            wchar_t* end = nullptr;
            const double value = std::wcstod(token.c_str(), &end);
            if (end == token.c_str())
            {
                ctx.failed = true;
                return Json::MakeNull();
            }
            return Json::MakeNumber(value);
        }

        ctx.failed = true;
        return Json::MakeNull();
    }

    void EscapeString(const std::wstring& value, std::wstring& out)
    {
        out.push_back(L'"');
        for (const wchar_t c : value)
        {
            switch (c)
            {
            case L'"':
                out += L"\\\"";
                break;
            case L'\\':
                out += L"\\\\";
                break;
            case L'\b':
                out += L"\\b";
                break;
            case L'\f':
                out += L"\\f";
                break;
            case L'\n':
                out += L"\\n";
                break;
            case L'\r':
                out += L"\\r";
                break;
            case L'\t':
                out += L"\\t";
                break;
            default:
                if (static_cast<unsigned>(c) < 0x20)
                {
                    wchar_t buf[8];
                    swprintf_s(buf, L"\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                }
                else
                {
                    out.push_back(c);
                }
                break;
            }
        }
        out.push_back(L'"');
    }

    void AppendIndent(std::wstring& out, int level)
    {
        out.append(static_cast<size_t>(level) * 2, L' ');
    }
}

Json::Json() = default;

Json Json::MakeNull()
{
    return Json();
}

Json Json::MakeBool(bool value)
{
    Json j;
    j.m_type = Type::Bool;
    j.m_bool = value;
    return j;
}

Json Json::MakeNumber(double value)
{
    Json j;
    j.m_type = Type::Number;
    j.m_number = value;
    return j;
}

Json Json::MakeString(const std::wstring& value)
{
    Json j;
    j.m_type = Type::String;
    j.m_string = value;
    return j;
}

Json Json::MakeArray()
{
    Json j;
    j.m_type = Type::Array;
    return j;
}

Json Json::MakeObject()
{
    Json j;
    j.m_type = Type::Object;
    return j;
}

Json::Type Json::type() const
{
    return m_type;
}

bool Json::isNull() const
{
    return m_type == Type::Null;
}

bool Json::Has(const std::wstring& key) const
{
    return Find(key) != nullptr;
}

const Json* Json::Find(const std::wstring& key) const
{
    if (m_type != Type::Object)
    {
        return nullptr;
    }
    const auto it = std::find_if(m_object.begin(), m_object.end(), [&key](const auto& entry) { return entry.first == key; });
    return it == m_object.end() ? nullptr : &it->second;
}

const Json& Json::At(const std::wstring& key) const
{
    const Json* found = Find(key);
    return found ? *found : kNullJson;
}

void Json::Set(const std::wstring& key, const Json& value)
{
    if (m_type != Type::Object)
    {
        m_type = Type::Object;
        m_object.clear();
    }
    const auto it = std::find_if(m_object.begin(), m_object.end(), [&key](const auto& entry) { return entry.first == key; });
    if (it == m_object.end())
    {
        m_object.emplace_back(key, value);
    }
    else
    {
        it->second = value;
    }
}

void Json::Set(const std::wstring& key, bool value)
{
    Set(key, MakeBool(value));
}

void Json::Set(const std::wstring& key, double value)
{
    Set(key, MakeNumber(value));
}

void Json::Set(const std::wstring& key, const std::wstring& value)
{
    Set(key, MakeString(value));
}

void Json::Push(const Json& value)
{
    if (m_type != Type::Array)
    {
        m_type = Type::Array;
        m_array.clear();
    }
    m_array.push_back(value);
}

size_t Json::Size() const
{
    if (m_type == Type::Array)
    {
        return m_array.size();
    }
    if (m_type == Type::Object)
    {
        return m_object.size();
    }
    return 0;
}

const Json& Json::At(size_t index) const
{
    if (m_type == Type::Array && index < m_array.size())
    {
        return m_array[index];
    }
    return kNullJson;
}

double Json::AsNumber(double fallback) const
{
    return m_type == Type::Number ? m_number : fallback;
}

bool Json::AsBool(bool fallback) const
{
    return m_type == Type::Bool ? m_bool : fallback;
}

std::wstring Json::AsString(const std::wstring& fallback) const
{
    return m_type == Type::String ? m_string : fallback;
}

void Json::AppendSerialized(std::wstring& out, int indentLevel, bool compact) const
{
    switch (m_type)
    {
    case Type::Null:
        out += L"null";
        break;
    case Type::Bool:
        out += m_bool ? L"true" : L"false";
        break;
    case Type::Number:
    {
        if (std::floor(m_number) == m_number && std::abs(m_number) < 1e15)
        {
            wchar_t buf[32];
            swprintf_s(buf, L"%lld", static_cast<long long>(m_number));
            out += buf;
        }
        else
        {
            wchar_t buf[32];
            swprintf_s(buf, L"%.9g", m_number);
            out += buf;
        }
        break;
    }
    case Type::String:
        EscapeString(m_string, out);
        break;
    case Type::Array:
    {
        if (m_array.empty())
        {
            out += L"[]";
            break;
        }
        out += L'[';
        for (size_t i = 0; i < m_array.size(); ++i)
        {
            if (i > 0)
            {
                out += L',';
            }
            if (!compact)
            {
                out += L'\n';
                AppendIndent(out, indentLevel + 1);
            }
            m_array[i].AppendSerialized(out, indentLevel + 1, compact);
        }
        if (!compact)
        {
            out += L'\n';
            AppendIndent(out, indentLevel);
        }
        out += L']';
        break;
    }
    case Type::Object:
    {
        if (m_object.empty())
        {
            out += L"{}";
            break;
        }
        out += L'{';
        for (size_t i = 0; i < m_object.size(); ++i)
        {
            if (i > 0)
            {
                out += L',';
            }
            if (!compact)
            {
                out += L'\n';
                AppendIndent(out, indentLevel + 1);
            }
            EscapeString(m_object[i].first, out);
            out += L':';
            if (!compact)
            {
                out += L' ';
            }
            m_object[i].second.AppendSerialized(out, indentLevel + 1, compact);
        }
        if (!compact)
        {
            out += L'\n';
            AppendIndent(out, indentLevel);
        }
        out += L'}';
        break;
    }
    }
}

std::wstring Json::SerializeIndented() const
{
    std::wstring out;
    AppendSerialized(out, 0, false);
    return out;
}

std::wstring Json::SerializeCompact() const
{
    std::wstring out;
    AppendSerialized(out, 0, true);
    return out;
}

bool Json::Parse(const std::wstring& text, Json& out)
{
    ParseContext ctx;
    ctx.pos = text.c_str();
    ctx.failed = false;

    const Json value = ParseValue(ctx);
    SkipWhitespace(ctx);
    if (ctx.failed || *ctx.pos != L'\0')
    {
        return false;
    }

    out = value;
    return true;
}

#pragma once

#include <string>
#include <vector>
#include <utility>

// Minimal JSON value + recursive-descent parser/serializer (UTF-8 I/O, wide internal).
// Deliberately tiny: we control the schema of every file LiteDDC reads/writes.
class Json
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Json();
    static Json MakeNull();
    static Json MakeBool(bool value);
    static Json MakeNumber(double value);
    static Json MakeString(const std::wstring& value);
    static Json MakeArray();
    static Json MakeObject();

    Type type() const;
    bool isNull() const;

    // Object access
    bool Has(const std::wstring& key) const;
    const Json* Find(const std::wstring& key) const;
    const Json& At(const std::wstring& key) const;
    void Set(const std::wstring& key, const Json& value);
    void Set(const std::wstring& key, bool value);
    void Set(const std::wstring& key, double value);
    void Set(const std::wstring& key, const std::wstring& value);

    // Array access
    void Push(const Json& value);
    size_t Size() const;
    const Json& At(size_t index) const;

    // Typed accessors with defaults
    double AsNumber(double fallback = 0.0) const;
    bool AsBool(bool fallback = false) const;
    std::wstring AsString(const std::wstring& fallback = L"") const;

    // Serialize. Indented uses 2-space indent; compact writes on one line.
    std::wstring SerializeIndented() const;
    std::wstring SerializeCompact() const;

    // Parse; returns false on malformed input.
    static bool Parse(const std::wstring& text, Json& out);

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::wstring m_string;
    std::vector<Json> m_array;
    std::vector<std::pair<std::wstring, Json>> m_object;

    void AppendSerialized(std::wstring& out, int indentLevel, bool compact) const;
};

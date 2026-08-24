#include "Settings.h"

#include "Paths.h"

Settings& Settings::instance()
{
    static Settings s_instance;
    return s_instance;
}

Json Settings::SerializeData(const SettingsData& data)
{
    Json json = Json::MakeObject();
    json.Set(L"brightnessEnabled", data.brightnessEnabled);
    json.Set(L"contrastEnabled", data.contrastEnabled);
    json.Set(L"saturationEnabled", data.saturationEnabled);
    json.Set(L"scrollStep", static_cast<double>(data.scrollStep));
    json.Set(L"scope", static_cast<double>(data.scope));
    json.Set(L"lastBrightness", static_cast<double>(data.lastBrightness));
    json.Set(L"lastContrast", static_cast<double>(data.lastContrast));
    json.Set(L"lastSaturation", static_cast<double>(data.lastSaturation));
    json.Set(L"lastRed", static_cast<double>(data.lastRed));
    json.Set(L"lastGreen", static_cast<double>(data.lastGreen));
    json.Set(L"lastBlue", static_cast<double>(data.lastBlue));
    json.Set(L"lastVibrance", static_cast<double>(data.lastVibrance));
    json.Set(L"lastVolume", static_cast<double>(data.lastVolume));
    return json;
}

bool Settings::DeserializeData(const Json& json, SettingsData& out)
{
    if (json.type() != Json::Type::Object)
    {
        return false;
    }

    // Missing keys fall back to whatever |out| already holds (callers pass
    // a default-constructed struct, so that means sane defaults).
    out.brightnessEnabled = json.At(L"brightnessEnabled").AsBool(out.brightnessEnabled);
    out.contrastEnabled = json.At(L"contrastEnabled").AsBool(out.contrastEnabled);
    out.saturationEnabled = json.At(L"saturationEnabled").AsBool(out.saturationEnabled);

    const double step = json.At(L"scrollStep").AsNumber(static_cast<double>(out.scrollStep));
    out.scrollStep = step >= 1.0 ? static_cast<int>(step) : 1;

    const double scope = json.At(L"scope").AsNumber(static_cast<double>(out.scope));
    out.scope = static_cast<int>(scope);

    // Range-guard the slider memories: anything impossible is corruption,
    // and corruption resets to "never set" rather than propagating garbage.
    const auto readLast = [&json](const wchar_t* key, int fallback) {
        const double value = json.At(key).AsNumber(static_cast<double>(fallback));
        return (value >= 0.0 && value <= 100.0) ? static_cast<int>(value) : -1;
    };
    out.lastBrightness = readLast(L"lastBrightness", out.lastBrightness);
    out.lastContrast = readLast(L"lastContrast", out.lastContrast);
    out.lastSaturation = readLast(L"lastSaturation", out.lastSaturation);
    out.lastRed = readLast(L"lastRed", out.lastRed);
    out.lastGreen = readLast(L"lastGreen", out.lastGreen);
    out.lastBlue = readLast(L"lastBlue", out.lastBlue);
    out.lastVibrance = readLast(L"lastVibrance", out.lastVibrance);
    out.lastVolume = readLast(L"lastVolume", out.lastVolume);

    return true;
}

void Settings::Load()
{
    std::wstring text;
    if (!Paths::ReadTextFile(Paths::SettingsFile(), text))
    {
        return; // first run or unreadable dir: defaults
    }
    Json json;
    if (!Json::Parse(text, json))
    {
        return; // corrupt file: defaults (roadmap defensive-load rule)
    }
    SettingsData parsed;
    if (!DeserializeData(json, parsed))
    {
        return; // wrong top-level shape: defaults
    }
    m_data = parsed;
    m_dirty = false;
}

void Settings::Save()
{
    if (!Paths::EnsureConfigDir())
    {
        return;
    }
    if (Paths::WriteTextFile(Paths::SettingsFile(), SerializeData(m_data).SerializeIndented(), false))
    {
        m_dirty = false;
    }
}

void Settings::SaveIfDirty()
{
    if (m_dirty)
    {
        Save();
    }
}

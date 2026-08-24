#include "TestHarness.h"

#include "../src/data/Settings.h"

// Pure-codec tests only: SerializeData/DeserializeData touch no filesystem,
// so these never write to the real %LOCALAPPDATA%.

namespace
{
    SettingsData RoundTrip(const SettingsData& in)
    {
        const Json json = Settings::SerializeData(in);
        const std::wstring text = json.SerializeIndented();

        Json parsed;
        CHECK(Json::Parse(text, parsed));

        SettingsData out;
        CHECK(Settings::DeserializeData(parsed, out));
        return out;
    }
}

void RunSettingsTests()
{
    // Defaults round-trip unchanged.
    {
        const SettingsData out = RoundTrip(SettingsData{});
        CHECK(out.brightnessEnabled == true);
        CHECK(out.contrastEnabled == true);
        CHECK(out.saturationEnabled == true);
        CHECK(out.scrollStep == 2);
        CHECK(out.scope == -1);
        CHECK(out.lastBrightness == -1);
        CHECK(out.lastContrast == -1);
        CHECK(out.lastSaturation == -1);
    }

    // Non-default values survive verbatim.
    {
        SettingsData in;
        in.brightnessEnabled = false;
        in.contrastEnabled = false;
        in.saturationEnabled = true;
        in.scrollStep = 10;
        in.scope = 0;
        in.lastBrightness = 73;
        in.lastContrast = 89;
        in.lastSaturation = 50;

        const SettingsData out = RoundTrip(in);
        CHECK(out.brightnessEnabled == false);
        CHECK(out.contrastEnabled == false);
        CHECK(out.saturationEnabled == true);
        CHECK(out.scrollStep == 10);
        CHECK(out.scope == 0);
        CHECK(out.lastBrightness == 73);
        CHECK(out.lastContrast == 89);
        CHECK(out.lastSaturation == 50);
    }

    // Non-object top level is rejected (corrupt file shape).
    {
        SettingsData out;
        CHECK(!Settings::DeserializeData(Json::MakeString(L"garbage"), out));
        CHECK(!Settings::DeserializeData(Json::MakeNumber(42), out));
    }

    // Missing keys fall back to defaults already present in |out|.
    {
        SettingsData out; // defaults
        CHECK(Settings::DeserializeData(Json::MakeObject(), out));
        CHECK(out.scrollStep == 2);
        CHECK(out.brightnessEnabled == true);
        CHECK(out.lastBrightness == -1);
    }

    // Impossible scrollStep clamps to >= 1.
    {
        Json json = Json::MakeObject();
        json.Set(L"scrollStep", 0.0);
        SettingsData out;
        CHECK(Settings::DeserializeData(json, out));
        CHECK(out.scrollStep == 1);

        json.Set(L"scrollStep", -5.0);
        CHECK(Settings::DeserializeData(json, out));
        CHECK(out.scrollStep == 1);
    }

    // Out-of-range slider memories reset to "never set" (-1), not garbage.
    {
        Json json = Json::MakeObject();
        json.Set(L"lastBrightness", 250.0);
        json.Set(L"lastContrast", -3.0);
        json.Set(L"lastSaturation", 55.0); // valid one survives

        SettingsData out;
        CHECK(Settings::DeserializeData(json, out));
        CHECK(out.lastBrightness == -1);
        CHECK(out.lastContrast == -1);
        CHECK(out.lastSaturation == 55);
    }
}

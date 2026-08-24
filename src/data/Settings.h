#pragma once

#include "json.h"

/// Persisted user preferences (architecture doc section 9): a plain struct
/// with sane defaults, loaded/saved as a whole to
/// %LOCALAPPDATA%\LiteDDC\settings.json via the reused json.cpp/Paths.
///
/// Threading contract: UI thread only (dialog handlers, menu handlers,
/// App::Init before the worker starts). AdjustmentCoordinator keeps its own
/// mutex-guarded runtime copy of the live values - Settings is the
/// persistence snapshot pushed into it at startup, not the runtime
/// authority.
struct SettingsData
{
    // Scroll-gesture enablement (the dialog checkboxes gate SCROLLING only;
    // sliders always work on supported parameters). R/G/B gains and Volume
    // have no checkbox at all - they are slider-only and never scroll-driven
    // (M12/M14 human decisions).
    bool brightnessEnabled = true;
    bool contrastEnabled = true;
    bool saturationEnabled = true;

    int scrollStep = 2; // percent per wheel notch, clamped >= 1
    int scope = -1;     // -1 = all monitors, else logical monitor index

    // Last values applied through the dialog sliders (-1 = never set).
    // Deliberately NOT re-applied to hardware at launch: monitors silently
    // waking up at changed brightness would be surprising. Persisted so the
    // state survives for future milestones / diagnostics.
    int lastBrightness = -1;
    int lastContrast = -1;
    int lastSaturation = -1;
    int lastRed = -1;
    int lastGreen = -1;
    int lastBlue = -1;
    // M13 GPU vibrance slider memory (same "never re-applied at launch"
    // semantics as the DDC rows above).
    int lastVibrance = -1;
    // M14 monitor volume slider memory.
    int lastVolume = -1;
};

class Settings
{
public:
    static Settings& instance();

    /// Direct access for UI-thread mutations; pair changes with Save() or
    /// MarkDirty()/SaveIfDirty() for high-frequency paths (slider drags).
    SettingsData& data() { return m_data; }
    void MarkDirty() { m_dirty = true; }
    void SaveIfDirty();

    /// Missing or corrupt file/fields fall back to struct defaults -
    /// never throws, never crashes startup.
    void Load();
    void Save();

    /// Pure JSON codec (no filesystem), public so unit tests can exercise
    /// round-trips and defensive parsing without touching %LOCALAPPDATA%.
    static Json SerializeData(const SettingsData& data);
    static bool DeserializeData(const Json& json, SettingsData& out);

private:
    Settings() = default;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    SettingsData m_data;
    bool m_dirty = false;
};

#pragma once

#include <string>

// File-system helpers: config directory paths and UTF-8 text read/write.
namespace Paths
{
    // %LOCALAPPDATA%\LiteDDC
    std::wstring ConfigDir();

    // Individual data files.
    std::wstring SettingsFile();

    // Creates the config directory if missing.
    bool EnsureConfigDir();

    // UTF-8 file I/O (read strips BOM; write is UTF-8 without BOM).
    bool ReadTextFile(const std::wstring& path, std::wstring& out);
    bool WriteTextFile(const std::wstring& path, const std::wstring& text, bool crlf);
}

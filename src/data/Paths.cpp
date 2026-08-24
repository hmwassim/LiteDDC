#include "Paths.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

namespace
{
    constexpr wchar_t kConfigDirName[] = L"LiteDDC";
    constexpr wchar_t kSettingsFileName[] = L"settings.json";
}

namespace Paths
{
    std::wstring ConfigDir()
    {
        PWSTR localAppData = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
        {
            return L"";
        }
        std::wstring result = localAppData;
        CoTaskMemFree(localAppData);
        result += L"\\";
        result += kConfigDirName;
        return result;
    }

    std::wstring SettingsFile()
    {
        return ConfigDir() + L"\\" + kSettingsFileName;
    }

    bool EnsureConfigDir()
    {
        const std::wstring dir = ConfigDir();
        if (dir.empty())
        {
            return false;
        }
        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return CreateDirectoryW(dir.c_str(), nullptr) != FALSE;
        }
        return true;
    }

    bool ReadTextFile(const std::wstring& path, std::wstring& out)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0)
        {
            CloseHandle(file);
            return false;
        }
        if (size.QuadPart > (8 * 1024 * 1024))
        {
            CloseHandle(file);
            return false;
        }

        std::string utf8(static_cast<size_t>(size.QuadPart), '\0');
        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &bytesRead, nullptr);
        CloseHandle(file);
        if (!ok || bytesRead != utf8.size())
        {
            return false;
        }

        if (utf8.size() >= 3 && static_cast<unsigned char>(utf8[0]) == 0xEF && static_cast<unsigned char>(utf8[1]) == 0xBB && static_cast<unsigned char>(utf8[2]) == 0xBF)
        {
            utf8.erase(0, 3);
        }

        const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (wideLen <= 0)
        {
            return false;
        }
        out.resize(static_cast<size_t>(wideLen));
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), wideLen);
        return true;
    }

    bool WriteTextFile(const std::wstring& path, const std::wstring& text, bool crlf)
    {
        std::wstring normalized;
        if (crlf)
        {
            normalized.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
                {
                    normalized += L"\r\n";
                }
                else
                {
                    normalized.push_back(text[i]);
                }
            }
        }
        else
        {
            normalized = text;
        }

        const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, normalized.c_str(), static_cast<int>(normalized.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0)
        {
            return false;
        }
        std::string utf8(static_cast<size_t>(utf8Len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, normalized.c_str(), static_cast<int>(normalized.size()), utf8.data(), utf8Len, nullptr, nullptr);

        // Atomic write: write to a temporary file next to the target, then
        // rename over it.  If the process crashes mid-write the original file
        // is untouched.
        const std::wstring tempPath = path + L".tmp";
        HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        DWORD written = 0;
        const BOOL ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);

        if (!ok || written != utf8.size())
        {
            DeleteFileW(tempPath.c_str());
            return false;
        }

        if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            DeleteFileW(tempPath.c_str());
            return false;
        }
        return true;
    }
}

#include "CapabilitiesParser.h"

#include <algorithm>
#include <cstring>

namespace
{
    const char* FindVcpSegmentStart(const std::string& text)
    {
        const char needle[] = "vcp(";
        const size_t needleLen = std::strlen(needle);
        for (size_t i = 0; i + needleLen <= text.size(); ++i)
        {
            if (_strnicmp(text.c_str() + i, needle, needleLen) == 0)
            {
                return text.c_str() + i + needleLen;
            }
        }
        return nullptr;
    }
}

std::set<BYTE> CapabilitiesParser::ParseSupportedVcpCodes(const std::string& rawCapabilityString)
{
    std::set<BYTE> codes;

    const char* cursor = FindVcpSegmentStart(rawCapabilityString);
    if (!cursor)
    {
        return codes;
    }

    // Depth starts at 1 because the opening '(' of vcp( was consumed above.
    int depth = 1;
    unsigned hexAccumulator = 0;
    size_t hexDigits = 0;
    while (*cursor != '\0' && depth > 0)
    {
        const char ch = *cursor;
        if (ch == '(')
        {
            ++depth; // supported-values list belonging to the preceding code
            hexDigits = 0;
        }
        else if (ch == ')')
        {
            --depth;
            hexDigits = 0;
        }
        else if (ch >= '0' && ch <= '9' || ch >= 'a' && ch <= 'f' || ch >= 'A' && ch <= 'F')
        {
            if (depth == 1)
            {
                hexAccumulator = hexAccumulator * 16 + static_cast<unsigned>((ch <= '9') ? (ch - '0') : ((ch | 0x20) - 'a' + 10));
                ++hexDigits;
                if (hexDigits == 2)
                {
                    codes.insert(static_cast<BYTE>(hexAccumulator));
                    hexDigits = 0;
                    hexAccumulator = 0;
                }
            }
        }
        else
        {
            hexDigits = 0;
            hexAccumulator = 0;
        }
        ++cursor;
    }

    // An odd trailing hex digit at depth 1 is malformed input; ignore it.
    return codes;
}

#pragma once

#include <windows.h>

#include <string>
#include <set>

/// Pure parsing of MCCS capability strings - no I/O, no hardware access,
/// directly unit-testable without a monitor (roadmap M1).
///
/// A capability string looks like:
///   (prot(monitor)type(lcd)UL550cmds(01 02)vcp(10 12 14(05 08) 60(11 12))mccs_ver(2.1))
/// The vcp(...) segment lists supported VCP codes as 2-hex-digit tokens;
/// codes may be followed by a parenthesized list of supported values, which
/// is ignored here (we only need whether the code exists at all).
namespace CapabilitiesParser
{
    /// Extracts the set of VCP codes claimed supported by the vcp(...) segment
    /// of |rawCapabilityString|. Tolerant of malformed input: missing/empty
    /// vcp segment, unbalanced parentheses, or non-hex tokens yield an empty
    /// or partial set rather than an error. Duplicates collapse. Matching of
    /// the leading "vcp(" token is case-insensitive.
    std::set<BYTE> ParseSupportedVcpCodes(const std::string& rawCapabilityString);
}

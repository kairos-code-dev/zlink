// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal static class RegistryReadSupport
{
    internal static bool IsRetryableSizeRace(int errno)
    {
        return ZlinkException.MapErrorCode(errno) == ErrorCode.ENoBufs;
    }
}

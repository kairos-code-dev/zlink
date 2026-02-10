// SPDX-License-Identifier: MPL-2.0

using Zlink.Native;

namespace Zlink;

public static class ZlinkVersion
{
    public static (int Major, int Minor, int Patch) Get()
    {
        NativeMethods.zlink_version(out int major, out int minor, out int patch);
        return (major, minor, patch);
    }
}

// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static class ErrorInterop
{
    internal static int LastErrno() => NativeMethods.zlink_errno();
}

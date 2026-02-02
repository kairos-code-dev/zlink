using System;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static partial class NativeMethods
{
    [LibraryImport("zlink")]
    internal static partial void zlink_version(out int major, out int minor, out int patch);

    [LibraryImport("zlink")]
    internal static partial IntPtr zlink_ctx_new();

    [LibraryImport("zlink")]
    internal static partial int zlink_ctx_term(IntPtr context);

    [LibraryImport("zlink")]
    internal static partial IntPtr zlink_socket(IntPtr context, int type);

    [LibraryImport("zlink")]
    internal static partial int zlink_close(IntPtr socket);

    [LibraryImport("zlink")]
    internal static partial int zlink_errno();

    [LibraryImport("zlink")]
    internal static partial IntPtr zlink_strerror(int errnum);
}

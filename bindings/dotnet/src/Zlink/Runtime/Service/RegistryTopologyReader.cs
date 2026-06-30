// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class RegistryTopologyReader
{
    internal static RegistryTopologyEntry[] Read(IntPtr handle, IntPtr filter,
        ReadFn read)
    {
        for (var attempt = 0; attempt < 4; attempt++)
        {
            nuint count = 0;
            var rc = read(handle, filter, IntPtr.Zero, ref count);
            ZlinkException.ThrowConfigIfError(rc);
            if (count == 0)
                return Array.Empty<RegistryTopologyEntry>();

            var entrySize = Marshal.SizeOf<ZlinkRegistryTopologyEntry>();
            var entries = Marshal.AllocHGlobal(
                checked((int)(count * (nuint)entrySize)));
            try
            {
                var actual = count;
                rc = read(handle, filter, entries, ref actual);
                if (rc != 0 && IsRetryableSizeRace(NativeMethods.zlink_errno()))
                    continue;
                ZlinkException.ThrowConfigIfError(rc);

                var result =
                    new RegistryTopologyEntry[(int)actual];
                for (var i = 0; i < result.Length; i++)
                {
                    var current = IntPtr.Add(entries, i * entrySize);
                    var native =
                        Marshal.PtrToStructure<ZlinkRegistryTopologyEntry>(
                            current);
                    result[i] = TopologyModelConverters.FromNative(ref native);
                }

                return result;
            }
            finally
            {
                Marshal.FreeHGlobal(entries);
            }
        }

        throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    private static bool IsRetryableSizeRace(int errno)
    {
        return ZlinkException.MapErrorCode(errno) == ErrorCode.ENoBufs;
    }

    internal delegate int ReadFn(IntPtr handle, IntPtr filter, IntPtr entries,
        ref nuint count);
}
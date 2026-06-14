// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class RegistryTopologyReader
{
    internal delegate int ReadFn(IntPtr handle, IntPtr filter, IntPtr entries,
        ref nuint count);

    internal static RegistryTopologyEntry[] Read(IntPtr handle, IntPtr filter,
        ReadFn read)
    {
        for (int attempt = 0; attempt < 4; attempt++)
        {
            nuint count = 0;
            int rc = read(handle, filter, IntPtr.Zero, ref count);
            ZlinkException.ThrowConfigIfError(rc);
            if (count == 0)
                return Array.Empty<RegistryTopologyEntry>();

            int entrySize = Marshal.SizeOf<ZlinkRegistryTopologyEntry>();
            IntPtr entries = Marshal.AllocHGlobal(
                checked((int)(count * (nuint)entrySize)));
            try
            {
                nuint actual = count;
                rc = read(handle, filter, entries, ref actual);
                if (rc != 0 && IsRetryableSizeRace(NativeMethods.zlink_errno()))
                    continue;
                ZlinkException.ThrowConfigIfError(rc);

                RegistryTopologyEntry[] result =
                    new RegistryTopologyEntry[(int)actual];
                for (int i = 0; i < result.Length; i++)
                {
                    IntPtr current = IntPtr.Add(entries, i * entrySize);
                    ZlinkRegistryTopologyEntry native =
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
}

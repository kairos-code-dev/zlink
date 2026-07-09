// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink;

internal static class NativeSnapshotReader
{
    internal delegate int Fill(IntPtr entries, ref nuint count);

    internal delegate TManaged Convert<TNative, TManaged>(
        ref TNative native)
        where TNative : struct;

    internal static TManaged[] Read<TNative, TManaged>(
        Fill fill, Convert<TNative, TManaged> convert)
        where TNative : struct
    {
        if (fill == null)
            throw new ArgumentNullException(nameof(fill));
        if (convert == null)
            throw new ArgumentNullException(nameof(convert));

        nuint count = 0;
        var rc = fill(IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<TManaged>();

        var entrySize = Marshal.SizeOf<TNative>();
        var entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = fill(entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            var result = new TManaged[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var native = Marshal.PtrToStructure<TNative>(
                    IntPtr.Add(entries, i * entrySize));
                result[i] = convert(ref native);
            }

            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }
}

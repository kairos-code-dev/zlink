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
        var entrySize = Marshal.SizeOf<TNative>();
        while (true)
        {
            if (count == 0)
                return Array.Empty<TManaged>();

            var entries = Marshal.AllocHGlobal(
                checked((int)(count * (nuint)entrySize)));
            try
            {
                // Common service contract (core/doc/spec/core/service/README.md):
                // every caller-init output element must carry struct_size and
                // version before the native fill. These snapshot structs place
                // struct_size (uint32) then version (uint32) at offsets 0 and 4.
                for (var i = 0; i < (int)count; i++)
                {
                    var slot = IntPtr.Add(entries, i * entrySize);
                    Marshal.WriteInt32(slot, 0, entrySize);
                    Marshal.WriteInt32(slot, 4, 1);
                }

                var actual = count;
                rc = fill(entries, ref actual);
                if (rc == (int)ConfigResult.BufferTooSmall)
                {
                    // Snapshot membership can grow between the size query and
                    // the fill. Reserve extra capacity so sustained joins do
                    // not force one allocation per newly observed entry.
                    count = Math.Max(actual, checked(count * 2));
                    continue;
                }
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
}

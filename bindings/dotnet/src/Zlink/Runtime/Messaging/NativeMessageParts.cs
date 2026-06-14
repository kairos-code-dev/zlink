// SPDX-License-Identifier: MPL-2.0

using System;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class NativeMessageParts
{
    internal static void MoveToNative(ReadOnlySpan<Message> parts,
        Span<ZlinkMsg> nativeParts, string paramName, ref int built)
    {
        // Validate before the first move so a bad part cannot leave earlier
        // messages invalidated with no native owner to restore from.
        for (int i = 0; i < parts.Length; i++)
        {
            if (parts[i] == null)
            {
                throw new ArgumentException(
                    "Parts must not contain null messages.", paramName);
            }
        }

        for (int i = 0; i < parts.Length; i++)
        {
            parts[i].MoveTo(ref nativeParts[i]);
            built++;
        }
    }

    internal static void RestoreManaged(ReadOnlySpan<Message> parts,
        Span<ZlinkMsg> nativeParts, int start, int count)
    {
        // Restore in reverse construction order to mirror the native array
        // ownership transfer when multipart setup fails partway through.
        for (int i = start + count - 1; i >= start; i--)
            parts[i].RestoreFrom(ref nativeParts[i]);
    }
}

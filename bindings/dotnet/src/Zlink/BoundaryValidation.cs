// SPDX-License-Identifier: MPL-2.0

using System;
using System.Text;

namespace Systems.Zlink;

internal static class BoundaryValidation
{
    private const int FixedUtf8MaxBytes = 255;

    public static void ValidateFixedUtf8(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);

        int byteCount = Encoding.UTF8.GetByteCount(value);
        if (byteCount == 0 || byteCount > FixedUtf8MaxBytes)
        {
            throw new ArgumentOutOfRangeException(paramName,
                $"UTF-8 length must be between 1 and {FixedUtf8MaxBytes} bytes.");
        }
    }

    public static void ValidateOptionalFixedUtf8(string? value, string paramName)
    {
        if (string.IsNullOrEmpty(value))
            return;

        ValidateFixedUtf8(value, paramName);
    }
}

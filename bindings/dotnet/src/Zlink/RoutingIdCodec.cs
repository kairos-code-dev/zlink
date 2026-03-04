// SPDX-License-Identifier: MPL-2.0

using System;
using System.Text;

namespace Zlink;

internal static class RoutingIdCodec
{
    private const string HexPrefix = "hex:";

    internal static string ToPublicString(ReadOnlySpan<byte> routingId)
    {
        if (routingId.Length == 0)
            return string.Empty;

        string utf8 = Encoding.UTF8.GetString(routingId);
        if (IsPrintableUtf8Roundtrip(utf8, routingId))
            return utf8;

        return HexPrefix + Convert.ToHexString(routingId);
    }

    internal static byte[] FromPublicString(string routingId, string paramName)
    {
        if (routingId == null)
            throw new ArgumentNullException(paramName);
        if (routingId.Length == 0)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "routingId must not be empty.");
        }

        if (routingId.StartsWith(HexPrefix, StringComparison.OrdinalIgnoreCase))
        {
            string hex = routingId.Substring(HexPrefix.Length);
            if (hex.Length == 0 || (hex.Length & 1) != 0)
            {
                throw new ArgumentException(
                    "Hex routingId must contain an even number of digits.",
                    paramName);
            }

            byte[] bytes;
            try
            {
                bytes = Convert.FromHexString(hex);
            }
            catch (FormatException ex)
            {
                throw new ArgumentException(
                    "Invalid hex routingId format.",
                    paramName, ex);
            }

            if (bytes.Length == 0 || bytes.Length > 255)
            {
                throw new ArgumentOutOfRangeException(paramName,
                    "routingId length must be between 1 and 255 bytes.");
            }
            return bytes;
        }

        int byteCount = Encoding.UTF8.GetByteCount(routingId);
        if (byteCount <= 0 || byteCount > 255)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "routingId UTF-8 length must be between 1 and 255 bytes.");
        }

        byte[] encoded = new byte[byteCount];
        Encoding.UTF8.GetBytes(routingId, encoded.AsSpan());
        return encoded;
    }

    private static bool IsPrintableUtf8Roundtrip(string text,
        ReadOnlySpan<byte> original)
    {
        for (int i = 0; i < text.Length; i++)
        {
            if (char.IsControl(text[i]))
                return false;
        }

        int byteCount = Encoding.UTF8.GetByteCount(text);
        if (byteCount != original.Length)
            return false;

        byte[] roundtrip = new byte[byteCount];
        Encoding.UTF8.GetBytes(text, roundtrip.AsSpan());
        return roundtrip.AsSpan().SequenceEqual(original);
    }
}

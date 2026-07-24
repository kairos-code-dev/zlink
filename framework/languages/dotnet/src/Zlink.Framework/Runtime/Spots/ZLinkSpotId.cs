using System.Text;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotId
{
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);

    internal static bool IsValid(string? value)
    {
        if (string.IsNullOrEmpty(value) || value.IndexOf('\0') >= 0)
            return false;

        try
        {
            var byteCount = StrictUtf8.GetByteCount(value);
            return byteCount is >= 1 and <= byte.MaxValue;
        }
        catch (EncoderFallbackException)
        {
            return false;
        }
    }

    internal static string Require(string? value, string paramName)
    {
        if (!IsValid(value))
            throw new ArgumentException(
                "Spot ID must be valid UTF-8 with an encoded size of 1..255 bytes.",
                paramName);
        return value!;
    }

    internal static RoutingId ToNativeRoutingId(string value) =>
        RoutingId.From(StrictUtf8.GetBytes(Require(value, nameof(value))));

    internal static string FromNativeRoutingId(RoutingId value)
    {
        if (value.IsEmpty)
            return string.Empty;
        try
        {
            var decoded = StrictUtf8.GetString(value.ToBytes());
            return IsValid(decoded) ? decoded : string.Empty;
        }
        catch (DecoderFallbackException)
        {
            return string.Empty;
        }
    }
}

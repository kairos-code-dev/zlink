using System.Buffers.Binary;
using System.Globalization;
using System.Text;

namespace Zlink.Framework.Runtime.Spots;

internal readonly record struct ZLinkApplicationPayloadEnvelope(
    string PacketName,
    string ContentType,
    ReadOnlyMemory<byte> Payload);

internal static class ZLinkApplicationPayloadEnvelopeCodec
{
    private const byte Version = 1;

    internal static byte[] Encode(
        string packetName,
        string contentType,
        ReadOnlySpan<byte> payload)
    {
        var packet = Encoding.UTF8.GetBytes(packetName);
        var type = Encoding.UTF8.GetBytes(contentType);
        if (packet.Length > byte.MaxValue || type.Length > byte.MaxValue)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestProtocolError,
                "Application payload text fields must fit in 255 UTF-8 bytes.");
        var bodyLength = checked(1 + packet.Length + 1 + type.Length + 4 + payload.Length);
        var result = new byte[checked(1 + 4 + bodyLength)];
        var offset = 0;
        result[offset++] = Version;
        BinaryPrimitives.WriteUInt32BigEndian(
            result.AsSpan(offset, 4),
            checked((uint)bodyLength));
        offset += 4;
        result[offset++] = checked((byte)packet.Length);
        packet.CopyTo(result, offset);
        offset += packet.Length;
        result[offset++] = checked((byte)type.Length);
        type.CopyTo(result, offset);
        offset += type.Length;
        BinaryPrimitives.WriteUInt32BigEndian(
            result.AsSpan(offset, 4),
            checked((uint)payload.Length));
        offset += 4;
        payload.CopyTo(result.AsSpan(offset));
        return result;
    }

    internal static bool TryDecode(
        ReadOnlyMemory<byte> frame,
        out ZLinkApplicationPayloadEnvelope envelope)
    {
        envelope = default;
        var span = frame.Span;
        if (span.Length < 11 || span[0] != Version)
            return false;
        var bodyLength = BinaryPrimitives.ReadUInt32BigEndian(span.Slice(1, 4));
        if (bodyLength != span.Length - 5)
            return false;
        var offset = 5;
        var packetLength = span[offset++];
        if (span.Length - offset < packetLength + 1)
            return false;
        var packetName = Encoding.UTF8.GetString(span.Slice(offset, packetLength));
        offset += packetLength;
        var typeLength = span[offset++];
        if (span.Length - offset < typeLength + 4)
            return false;
        var contentType = Encoding.UTF8.GetString(span.Slice(offset, typeLength));
        offset += typeLength;
        var payloadLength = BinaryPrimitives.ReadUInt32BigEndian(span.Slice(offset, 4));
        offset += 4;
        if (payloadLength != span.Length - offset)
            return false;
        envelope = new ZLinkApplicationPayloadEnvelope(
            packetName,
            contentType,
            frame.Slice(offset));
        return true;
    }
}

internal static class ZLinkInlineCreationIntentCodec
{
    private const string Prefix = "inline-v1:";

    internal static string Encode(ReadOnlySpan<byte> payload)
    {
        var checksum = Zlink.Framework.Runtime.Locations.ZLinkCrc32C
            .Compute(payload);
        var encoded = Convert.ToBase64String(payload)
            .TrimEnd('=')
            .Replace('+', '-')
            .Replace('/', '_');
        return string.Create(
            CultureInfo.InvariantCulture,
            $"{Prefix}{checksum:x8}:{encoded}");
    }

    internal static bool TryDecode(
        string reference,
        out byte[] payload)
    {
        payload = [];
        if (!reference.StartsWith(Prefix, StringComparison.Ordinal))
            return false;
        var checksumEnd = reference.IndexOf(':', Prefix.Length);
        if (checksumEnd != Prefix.Length + 8
            || checksumEnd + 1 >= reference.Length
            || !uint.TryParse(
                reference.AsSpan(Prefix.Length, 8),
                NumberStyles.AllowHexSpecifier,
                CultureInfo.InvariantCulture,
                out var expectedChecksum))
            return false;
        var encoded = reference.AsSpan(checksumEnd + 1);
        foreach (var value in encoded)
            if (!(value is >= 'A' and <= 'Z'
                  or >= 'a' and <= 'z'
                  or >= '0' and <= '9'
                  or '-' or '_'))
                return false;
        if (encoded.Length % 4 == 1)
            return false;
        var padded = encoded.ToString()
            .Replace('-', '+')
            .Replace('_', '/')
            .PadRight((encoded.Length + 3) / 4 * 4, '=');
        try
        {
            payload = Convert.FromBase64String(padded);
        }
        catch (FormatException)
        {
            payload = [];
            return false;
        }
        if (Zlink.Framework.Runtime.Locations.ZLinkCrc32C.Compute(payload)
            == expectedChecksum)
            return true;
        payload = [];
        return false;
    }
}

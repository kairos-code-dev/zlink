using System.Buffers.Binary;
using System.Text;

namespace Zlink.Framework.Runtime.Registry;

internal static class ZLinkRegistryRoutePayloadCodec
{
    public static byte[] EncodeIdentity(
        byte version,
        string namespaceName,
        string identity,
        string tooLargeMessage)
    {
        var namespaceBytes = EncodeShortString(namespaceName, tooLargeMessage);
        var identityBytes = EncodeShortString(identity, tooLargeMessage);
        var value = new byte[
            1
            + sizeof(ushort) + namespaceBytes.Length
            + sizeof(ushort) + identityBytes.Length];
        var span = value.AsSpan();
        var offset = 0;
        span[offset++] = version;
        WriteUInt16(span, ref offset, namespaceBytes.Length);
        namespaceBytes.CopyTo(span[offset..]);
        offset += namespaceBytes.Length;
        WriteUInt16(span, ref offset, identityBytes.Length);
        identityBytes.CopyTo(span[offset..]);
        return value;
    }

    public static RegistryRouteIdentity DecodeIdentity(
        ReadOnlySpan<byte> bytes,
        byte expectedVersion,
        string invalidPayloadMessage)
    {
        EnsureRemaining(bytes, 0, 1, invalidPayloadMessage);
        if (bytes[0] != expectedVersion)
        {
            throw new FormatException(invalidPayloadMessage);
        }

        var offset = 1;
        var namespaceName = ReadString(bytes, ref offset, invalidPayloadMessage);
        var identity = ReadString(bytes, ref offset, invalidPayloadMessage);
        return new RegistryRouteIdentity(namespaceName, identity, offset);
    }

    public static string ReadString(
        ReadOnlySpan<byte> bytes,
        ref int offset,
        string invalidPayloadMessage)
    {
        var length = ReadUInt16(bytes, ref offset, invalidPayloadMessage);
        EnsureRemaining(bytes, offset, length, invalidPayloadMessage);
        var value = Encoding.UTF8.GetString(bytes.Slice(offset, length));
        offset += length;
        return value;
    }

    public static RoutingId ReadRoutingId(
        ReadOnlySpan<byte> bytes,
        ref int offset,
        string invalidPayloadMessage)
    {
        EnsureRemaining(bytes, offset, 1, invalidPayloadMessage);
        var length = bytes[offset++];
        EnsureRemaining(bytes, offset, length, invalidPayloadMessage);
        var routingId = RoutingId.FromBytes(bytes.Slice(offset, length));
        offset += length;
        return routingId;
    }

    public static ulong ReadUInt64(
        ReadOnlySpan<byte> bytes,
        ref int offset,
        string invalidPayloadMessage)
    {
        EnsureRemaining(bytes, offset, sizeof(ulong), invalidPayloadMessage);
        var value = BinaryPrimitives.ReadUInt64BigEndian(
            bytes.Slice(offset, sizeof(ulong)));
        offset += sizeof(ulong);
        return value;
    }

    public static void WriteUInt16(
        Span<byte> bytes,
        ref int offset,
        int value)
    {
        BinaryPrimitives.WriteUInt16BigEndian(
            bytes.Slice(offset, sizeof(ushort)),
            checked((ushort)value));
        offset += sizeof(ushort);
    }

    public static void WriteUInt64(
        Span<byte> bytes,
        ref int offset,
        ulong value)
    {
        BinaryPrimitives.WriteUInt64BigEndian(
            bytes.Slice(offset, sizeof(ulong)),
            value);
        offset += sizeof(ulong);
    }

    public static void EnsureFullyRead(
        ReadOnlySpan<byte> bytes,
        int offset,
        string invalidPayloadMessage)
    {
        if (bytes.Length != offset)
        {
            throw new FormatException(invalidPayloadMessage);
        }
    }

    public static void EnsureRemaining(
        ReadOnlySpan<byte> bytes,
        int offset,
        int length,
        string invalidPayloadMessage)
    {
        if (offset < 0 || length < 0 || bytes.Length < offset + length)
        {
            throw new FormatException(invalidPayloadMessage);
        }
    }

    private static ushort ReadUInt16(
        ReadOnlySpan<byte> bytes,
        ref int offset,
        string invalidPayloadMessage)
    {
        EnsureRemaining(bytes, offset, sizeof(ushort), invalidPayloadMessage);
        var value = BinaryPrimitives.ReadUInt16BigEndian(
            bytes.Slice(offset, sizeof(ushort)));
        offset += sizeof(ushort);
        return value;
    }

    private static byte[] EncodeShortString(
        string value,
        string tooLargeMessage)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        if (bytes.Length > ushort.MaxValue)
        {
            throw new ZLinkConfigurationException(tooLargeMessage);
        }

        return bytes;
    }
}

internal readonly record struct RegistryRouteIdentity(
    string Namespace,
    string Identity,
    int Offset)
{
    public bool Matches(
        string expectedNamespace,
        string expectedIdentity)
    {
        return string.Equals(Namespace, expectedNamespace, StringComparison.Ordinal)
            && string.Equals(Identity, expectedIdentity, StringComparison.Ordinal);
    }
}

namespace Zlink.Framework.Contracts.Codecs;

public readonly record struct ZLinkEncodedPayload(ReadOnlyMemory<byte> Bytes)
{
    public static ZLinkEncodedPayload From(byte[] bytes)
    {
        return new ZLinkEncodedPayload(bytes.ToArray());
    }

    public static ZLinkEncodedPayload From(ReadOnlyMemory<byte> bytes)
    {
        return new ZLinkEncodedPayload(bytes.ToArray());
    }

    public static ZLinkEncodedPayload From(ReadOnlySpan<byte> bytes)
    {
        return new ZLinkEncodedPayload(bytes.ToArray());
    }

    public byte[] ToArray()
    {
        return Bytes.ToArray();
    }
}

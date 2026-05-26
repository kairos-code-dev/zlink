
namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamFrameWriter
{
    public static void Write(
        Func<Message, bool> write,
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> payload,
        string failureMessage)
    {
        var frame = ZLinkStreamFrameCodec.Encode(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span, payload);
        using var payloadMessage = Message.From(frame);
        if (!write(payloadMessage))
        {
            throw new InvalidOperationException(failureMessage);
        }
    }

    public static void Write(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> payload,
        string failureMessage)
    {
        Write(message => stream.Write(message), header, payload.Span, failureMessage);
    }

    public static void Write(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> payload,
        string failureMessage)
    {
        Write(message => stream.Write(message), header, payload, failureMessage);
    }
}

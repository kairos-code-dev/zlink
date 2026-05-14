using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamFrameWriter
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();

    public static void Write(
        Func<Message, bool> write,
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> body,
        string failureMessage)
    {
        var frame = ZLinkStreamFrameCodec.Encode(HeaderCodec.Encode(header).Span, body);
        using var payloadMessage = Message.FromBytes(frame);
        if (!write(payloadMessage))
        {
            throw new InvalidOperationException(failureMessage);
        }
    }

    public static void Write(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> body,
        string failureMessage)
    {
        Write(message => stream.Write(message), header, body.Span, failureMessage);
    }

    public static void Write(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> body,
        string failureMessage)
    {
        Write(message => stream.Write(message), header, body, failureMessage);
    }
}

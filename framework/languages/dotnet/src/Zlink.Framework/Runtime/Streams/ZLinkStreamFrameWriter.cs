
namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamFrameWriter
{
    public static void Write(
        Func<Message, bool> write,
        IZlinkStreamHeaderCodec headerCodec,
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> payload,
        string failureMessage)
    {
        var frame = ZLinkStreamFrameCodec.Encode(headerCodec.Encode(header).Span, payload);
        using var payloadMessage = Message.FromBytes(frame);
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
        Write(message => stream.Write(message), ResolveHeaderCodec(stream), header, payload.Span, failureMessage);
    }

    public static void Write(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> payload,
        string failureMessage)
    {
        Write(message => stream.Write(message), ResolveHeaderCodec(stream), header, payload, failureMessage);
    }

    private static IZlinkStreamHeaderCodec ResolveHeaderCodec(IZLinkStream stream)
        => stream is ZLinkManagedStream managedStream
            ? managedStream.HeaderCodec
            : ZlinkStreamDefaultCodecs.Header();
}

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamControlFrames
{
    private const string HeartbeatPingName = "$zlink.heartbeat.ping";
    private const string HeartbeatPongName = "$zlink.heartbeat.pong";

    public static void Dispatch(
        ZLinkManagedStream stream,
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> payload)
    {
        if (payload.Length != 0)
        {
            throw new InvalidOperationException("Stream control packet payload must be empty.");
        }

        if (header.Name == HeartbeatPingName)
        {
            SendHeartbeatPong(stream);
            return;
        }

        if (header.Name == HeartbeatPongName)
        {
            return;
        }

        throw new InvalidOperationException("Unknown stream control packet.");
    }

    private static void SendHeartbeatPong(ZLinkManagedStream stream)
    {
        var pong = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Control,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            HeartbeatPongName,
            ZlinkStreamMetadata.Empty);
        ZLinkStreamFrameWriter.Write(
            stream,
            pong,
            ReadOnlySpan<byte>.Empty,
            "Stream heartbeat pong send failed.");
    }
}

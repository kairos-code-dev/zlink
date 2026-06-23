namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkSessionPacketHandler<in TSessionContext>
{
    string PacketName { get; }

    /// <remarks>
    /// The payload is the same framework <see cref="ZLinkMessage"/> shape used
    /// by <see cref="IZLinkSession.OnDispatchAsync"/>. Handlers may decode it
    /// or pass it to framework APIs.
    /// </remarks>
    ValueTask HandleAsync(
        TSessionContext context,
        ZlinkStreamHeader header,
        ZLinkMessage payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionPacketDispatcher<in TSessionContext>
{
    /// <summary>
    /// Dispatches only packets that have a registered session packet handler.
    /// </summary>
    /// <returns>
    /// <see langword="true"/> when a handler handled the packet; otherwise
    /// <see langword="false"/> so the session can decide whether to relay,
    /// reject, ignore, or log the packet.
    /// </returns>
    ValueTask<bool> TryHandleAsync(
        TSessionContext context,
        ZlinkStreamHeader header,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);
}

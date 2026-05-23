namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkSessionPacketHandler<in TSessionContext>
{
    string PacketName { get; }

    /// <remarks>
    /// The payload follows the same borrowed lifetime as
    /// <see cref="IZLinkSession.OnDispatchAsync"/>. Handlers may read it or
    /// pass it to framework APIs, but must not dispose it or move ownership.
    /// </remarks>
    ValueTask HandleAsync(
        TSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
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
        Message payload,
        CancellationToken cancellationToken = default);
}

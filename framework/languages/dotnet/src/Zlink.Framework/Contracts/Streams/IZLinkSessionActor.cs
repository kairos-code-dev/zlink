namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkSessionActor
{
    string ActorId => Ref.ActorId;

    ActorRef Ref { get; }

    /// <summary>
    /// Relays a stream packet to this bound actor without consuming the caller payload.
    /// </summary>
    /// <remarks>
    /// The framework creates any internal copy needed to cross queues or reach
    /// a remote ActorGateway. The caller remains responsible only for the
    /// lifetime it already owns. For inbound session callbacks that lifetime is
    /// managed by the framework runtime.
    /// </remarks>
    ValueTask RelayRawAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);

    ValueTask RelayAsync(
        ZlinkStreamHeader header,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);

    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}

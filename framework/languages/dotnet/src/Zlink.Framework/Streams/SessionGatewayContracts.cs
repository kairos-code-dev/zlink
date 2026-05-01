namespace Zlink.Framework.Streams;

public interface IZLinkSessionProxyHandler
{
    ValueTask<Message?> HandleAsync(
        IZLinkSessionProxyContext context,
        ZLinkActorRelayMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionProxyContext
{
    RoutingId SourceSessionNodeRid { get; }

    IZLinkSessionGateway SessionGateway { get; }
}

public interface IZLinkSessionGateway
{
    IZLinkSessionGatewaySendCall SendToActor<TMessage>(
        string routerChannelId,
        RoutingId targetSessionNodeRid,
        string actorId,
        TMessage message);

    IZLinkSessionGatewayRequestCall RequestActor<TRequest>(
        string routerChannelId,
        RoutingId targetSessionNodeRid,
        string actorId,
        TRequest request);
}

public interface IZLinkSessionGatewaySendCall
{
    IZLinkSessionGatewaySendCall WithPacketName(string packetName);

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionGatewayRequestCall
{
    IZLinkSessionGatewayRequestCall WithPacketName(string packetName);

    IZLinkSessionGatewayRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkActorRelay
{
    IZLinkActorRelaySendCall Send<TMessage>(TMessage message);

    IZLinkActorRelayRequestCall Request<TRequest>(TRequest request);

    ValueTask DispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorRelaySendCall
{
    IZLinkActorRelaySendCall WithPacketName(string packetName);

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkActorRelayRequestCall
{
    IZLinkActorRelayRequestCall WithPacketName(string packetName);

    IZLinkActorRelayRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<Message> Async(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionRequestCall
{
    IZLinkSessionRequestCall WithPacketName(string packetName);

    IZLinkSessionRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public readonly record struct ZLinkActorRelayEnvelope(
    string ActorId,
    ZlinkStreamHeader StreamHeader,
    bool ExpectsReply);

public readonly record struct ZLinkActorRelayMessage(
    ZLinkActorRelayEnvelope Envelope,
    Message Body);

public readonly record struct ZLinkSessionGatewayEnvelope(
    string ActorId,
    string PacketName,
    bool ExpectsReply);

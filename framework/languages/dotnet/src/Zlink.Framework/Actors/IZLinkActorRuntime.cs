namespace Zlink.Framework.Actors;

public interface IZLinkActorRuntime
{
    ValueTask AttachAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> JoinAsync<TRequest, TReply>(
        global::Zlink.RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TRequest : IZLinkRequest<TReply>;

    ValueTask DisconnectAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default);

    ValueTask SubmitAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        global::Zlink.Message body,
        CancellationToken cancellationToken = default);
}

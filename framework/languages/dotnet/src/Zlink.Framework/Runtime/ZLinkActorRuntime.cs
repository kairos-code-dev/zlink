namespace Zlink.Framework.Runtime;

internal sealed class ZLinkActorRuntime(ZLinkFrameworkRuntime runtime) : IZLinkActorRuntime
{
    public ValueTask AttachAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        ArgumentNullException.ThrowIfNull(stream);
        return runtime.AttachActorAsync(actor, stream, cancellationToken);
    }

    public ValueTask<TReply> JoinAsync<TRequest, TReply>(
        global::Zlink.RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TRequest : IZLinkRequest<TReply>
    {
        ArgumentNullException.ThrowIfNull(actor);
        ArgumentNullException.ThrowIfNull(request);
        return runtime.JoinActorAsync<TRequest, TReply>(spotRid, actor, request, cancellationToken);
    }

    public ValueTask DisconnectAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        ArgumentNullException.ThrowIfNull(stream);
        return runtime.DisconnectActorAsync(actor, stream, cancellationToken);
    }

    public ValueTask SubmitAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        global::Zlink.Message body,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        ArgumentNullException.ThrowIfNull(body);
        return runtime.SubmitActorAsync(actor, header, body, cancellationToken);
    }
}

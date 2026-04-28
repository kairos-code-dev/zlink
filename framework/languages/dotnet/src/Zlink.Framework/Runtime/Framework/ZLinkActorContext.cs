namespace Zlink.Framework.Runtime.Framework;

internal sealed class ZLinkActorContext(
    ZLinkFrameworkRuntime runtime,
    IZLinkActor actor,
    ZLinkActorRuntimeState state) : IZLinkActorContext
{
    private readonly IZLinkActorStreamClient _client = new ZLinkActorStreamClient(state);

    public string ActorKey => actor.ActorKey;

    public IZLinkStream? Stream => state.Stream;

    public IZLinkActorStreamClient? Client => state.Stream is null ? null : _client;

    public ZLinkSpot? Spot => state.Activation?.Spot;

    public ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        global::Zlink.RoutingId spotRid,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TRequest : IZLinkRequest<TReply>
    {
        return runtime.JoinActorAsync<TRequest, TReply>(spotRid, actor, request, cancellationToken);
    }
}

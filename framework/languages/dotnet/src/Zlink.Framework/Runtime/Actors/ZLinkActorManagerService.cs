namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorManagerService(ZLinkFrameworkRuntime runtime) : IZLinkActorManager
{
    public async ValueTask<ActorRef> CreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await CreateAsync(
                actorId,
                actorType,
                ZLinkMessage.Empty,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ActorRef> CreateAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default)
    {
        var result = await runtime.CreateActorAsync(
                actorId,
                actorType,
                createRequest,
                cancellationToken)
            .ConfigureAwait(false);
        return ActorRefFromState(result.Actor.ActorId);
    }

    public ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!runtime.TryGetCreatedActorState(actorId, out var state)
            || state.NativeActorRef is not { } actorRef)
            return ValueTask.FromResult<ActorRef?>(null);

        return ValueTask.FromResult<ActorRef?>(actorRef.ToNative());
    }

    public async ValueTask<ActorRef> GetOrCreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await GetOrCreateAsync(
                actorId,
                actorType,
                ZLinkMessage.Empty,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ActorRef> GetOrCreateAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default)
    {
        var result = await runtime.CreateLocalActorAsync(
                actorId,
                actorType,
                createRequest,
                cancellationToken)
            .ConfigureAwait(false);
        return ActorRefFromState(result.Actor.ActorId);
    }

    private ActorRef ActorRefFromState(string actorId)
    {
        var state = runtime.GetOrCreateActorState(actorId);
        var actorRef = state.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' does not have a native Actor ref.");
        return actorRef.ToNative();
    }
}

using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    private readonly ZLinkActorSessionRegistry _actorSessions = new();
    private ZLinkActorCreationCoordinator ActorCreation => _actorCreationInitialized
        ??= new ZLinkActorCreationCoordinator(runtime, services, getActorSpotNode, EnsureActorContext);
    private ZLinkActorDispatchRouter DispatchRouter => _dispatchRouterInitialized
        ??= new ZLinkActorDispatchRouter(runtime, services, _actorSessions, EnsureActorContext);

    private ZLinkActorCreationCoordinator? _actorCreationInitialized;
    private ZLinkActorDispatchRouter? _dispatchRouterInitialized;

    public async ValueTask<CreateActorResult> CreateAndBindActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actorId);
        return await ActorCreation.CreateAndBindActorAsync(
                state,
                actorId,
                actorType,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await DispatchRouter.SubmitByIdAsync(actorId, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return await DispatchRouter.SubmitForReplyAsync(actorId, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await DispatchRouter.SubmitAsync(actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask NotifyDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        await DispatchRouter.NotifyDisconnectedByIdAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
    }

    private ZLinkActorContext EnsureActorContext(
        IZLinkActor actor,
        ZLinkActorRuntimeState state)
    {
        if (state.Actor is not null
            && !ReferenceEquals(state.Actor, actor)
            && (state.SessionId is not null || state.Activation is not null))
        {
            throw new InvalidOperationException(
                $"Actor id '{actor.ActorId}' is already bound to another actor instance.");
        }

        if (!ReferenceEquals(state.Actor, actor))
        {
            state.Actor = actor;
            state.Context = null;
            state.IsConfigured = false;
            state.ClearPacketRegistrations();
        }

        if (state.Context is not { } context)
        {
            context = new ZLinkActorContext(runtime, actor, state);
            state.Context = context;
        }

        if (!ReferenceEquals(actor.Context, context))
        {
            actor.Context = context;
        }

        if (!state.IsConfigured)
        {
            state.IsConfigured = true;
            try
            {
                actor.Configure();
            }
            catch
            {
                state.IsConfigured = false;
                throw;
            }
        }

        return context;
    }

    public ZLinkActorRuntimeState GetOrCreateState(string actorId)
    {
        return _actorSessions.GetOrCreate(actorId);
    }

}

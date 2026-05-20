using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    private readonly ZLinkActorSessionRegistry _actorSessions = new();
    private ZLinkActorCreationCoordinator ActorCreation => _actorCreationInitialized
        ??= new ZLinkActorCreationCoordinator(
            runtime,
            services,
            getActorSpotNode,
            EnsureActorContext,
            BindActorContext);
    private ZLinkActorDispatchRouter DispatchRouter => _dispatchRouterInitialized
        ??= new ZLinkActorDispatchRouter(runtime, services, _actorSessions, BindActorContext);

    private ZLinkActorCreationCoordinator? _actorCreationInitialized;
    private ZLinkActorDispatchRouter? _dispatchRouterInitialized;

    public async ValueTask<CreateActorResult> CreateAndBindActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await CreateAndBindActorAsync(
                actorId,
                actorType,
                failIfExists: false,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await CreateAndBindActorAsync(
                actorId,
                actorType,
                failIfExists: true,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask<IZLinkActor?> FindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(
            _actorSessions.TryGet(actorId, out var state)
                ? state.Actor
                : null);
    }

    public bool TryGetCreatedActor(
        string actorId,
        string actorType,
        out IZLinkActor actor)
    {
        actor = null!;
        if (!_actorSessions.TryGet(actorId, out var state)
            || state.Actor is not { } existing)
        {
            return false;
        }

        if (state.ActorType is not null
            && !string.Equals(state.ActorType, actorType, StringComparison.Ordinal))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorTypeMismatch,
                $"Actor '{actorId}' already uses actor type '{state.ActorType}', not '{actorType}'.");
        }

        actor = existing;
        return true;
    }

    public bool TryGetCreatedActorState(
        string actorId,
        string actorType,
        out ZLinkActorRuntimeState state)
    {
        state = null!;
        if (!_actorSessions.TryGet(actorId, out var existingState)
            || existingState.Actor is null)
        {
            return false;
        }

        if (existingState.ActorType is not null
            && !string.Equals(existingState.ActorType, actorType, StringComparison.Ordinal))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorTypeMismatch,
                $"Actor '{actorId}' already uses actor type '{existingState.ActorType}', not '{actorType}'.");
        }

        state = existingState;
        return true;
    }

    private async ValueTask<CreateActorResult> CreateAndBindActorAsync(
        string actorId,
        string actorType,
        bool failIfExists,
        CancellationToken cancellationToken)
    {
        var state = _actorSessions.GetOrCreate(actorId);
        return await ActorCreation.CreateAndBindActorAsync(
                state,
                actorId,
                actorType,
                failIfExists,
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

    private ZLinkActorContext BindActorContext(
        IZLinkActor actor,
        ZLinkActorRuntimeState state)
    {
        if (!string.Equals(state.ActorId, actor.ActorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Actor state id '{state.ActorId}' does not match actor id '{actor.ActorId}'.");
        }

        if (state.Actor is not null
            && !ReferenceEquals(state.Actor, actor)
            && (state.SessionId is not null || state.Activation is not null))
        {
            throw new InvalidOperationException(
                $"Actor id '{actor.ActorId}' is already bound to another actor instance.");
        }

        var assignedActor = false;
        if (!ReferenceEquals(state.Actor, actor))
        {
            state.Actor = actor;
            state.IsConfigured = false;
            state.ClearPacketRegistrations();
            assignedActor = true;
        }

        var context = EnsureActorContext(state);
        if (!ReferenceEquals(actor.Context, context))
        {
            throw new InvalidOperationException(
                $"Actor '{actor.ActorId}' must expose the context provided by its factory.");
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
                if (assignedActor)
                {
                    state.Actor = null;
                    state.ClearPacketRegistrations();
                }

                throw;
            }
        }

        return context;
    }

    private ZLinkActorContext EnsureActorContext(ZLinkActorRuntimeState state)
    {
        if (state.Context is not { } context)
        {
            context = new ZLinkActorContext(runtime, state);
            state.Context = context;
        }

        return context;
    }

    public ZLinkActorRuntimeState GetOrCreateState(string actorId)
    {
        return _actorSessions.GetOrCreate(actorId);
    }

}

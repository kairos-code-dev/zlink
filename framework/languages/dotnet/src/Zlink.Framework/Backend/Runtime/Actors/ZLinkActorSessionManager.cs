using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorSessionManager(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    private readonly ZLinkActorSessionRegistry _actorSessions = new();
    private ZLinkActorDispatchRouter DispatchRouter => _dispatchRouterInitialized
        ??= new ZLinkActorDispatchRouter(runtime, services, _actorSessions, EnsureActorContext);

    private ZLinkActorDispatchRouter? _dispatchRouterInitialized;

    public async ValueTask<CreateActorResult> CreateAndBindActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        if (!runtime.Registration.ActorFactories.TryGetValue(actorType, out var factoryType))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                $"Actor factory '{actorType}' is not registered.");
        }

        var state = _actorSessions.GetOrCreate(actorId);
        var existingActor = await state.ExecuteLockedAsync(
            () => state.Actor,
            cancellationToken).ConfigureAwait(false);
        if (existingActor is not null)
        {
            return new CreateActorResult(existingActor, false);
        }

        await using var scope = services.CreateAsyncScope();
        var factory = (IZLinkActorFactory)scope.ServiceProvider.GetRequiredService(factoryType);
        var actor = await factory.CreateAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        if (actor is null)
        {
            throw new InvalidOperationException($"Actor factory '{factoryType}' returned null.");
        }

        if (!string.Equals(actor.ActorId, actorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Actor factory '{factoryType}' returned actor id '{actor.ActorId}' for requested id '{actorId}'.");
        }

        EnsureActorContext(actor, state);

        var node = getActorSpotNode();
        if (node is not null && state.NativeActorRef is null)
        {
            var existingRef = node.ActorLookup(actor.ActorId);
            state.NativeActorRef = existingRef ?? node.CreateActor(actor.ActorId);
        }

        return new CreateActorResult(actor, true);
    }

    public async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        await DispatchRouter.SubmitByIdAsync(actorId, header, body, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        return await DispatchRouter.SubmitForReplyAsync(actorId, header, body, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        EnsureActorContext(actor, state);
        ZLinkSpotActivation? previousActivation;

        previousActivation = await state.ExecuteLockedAsync(
            () => state.Activation,
            cancellationToken).ConfigureAwait(false);

        if (ReferenceEquals(previousActivation, activation))
        {
            return;
        }

        if (previousActivation is not null && !previousActivation.IsDisposed)
        {
            await previousActivation.LeaveActorAsync(actor, cancellationToken).ConfigureAwait(false);
        }

        await state.ExecuteLockedAsync(
            () =>
        {
            state.Activation = activation;
        },
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask LeaveActorFromSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        EnsureActorContext(actor, state);
        var shouldPrune = false;
        var currentSpotRid = activation.SpotRid;

        shouldPrune = await state.ExecuteLockedAsync(
            () =>
        {
            if (ReferenceEquals(state.Activation, activation))
            {
                state.Activation = null;
                return state.SessionId is null;
            }

            return false;
        },
            cancellationToken).ConfigureAwait(false);

        var node = getActorSpotNode();
        if (node is not null && state.NativeActorRef is { } actorRef)
        {
            try
            {
                await node.LeaveActorAsync(
                        actorRef,
                        currentSpotRid,
                        runtime.Registration.DefaultTimeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (ZlinkException)
            {
            }
        }

        if (shouldPrune)
        {
            _actorSessions.TryRemove(actor.ActorId, state);
        }
    }

    public async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        EnsureActorContext(actor, state);
        await state.ExecuteLockedAsync(
            () =>
        {
            state.SessionId = stream.SessionId;
            state.Stream = stream;
        },
            cancellationToken).ConfigureAwait(false);

        var node = getActorSpotNode();
        if (node is not null
            && state.NativeActorRef is { } actorRef
            && stream is ZLinkManagedStream managedStream)
        {
            await managedStream.BindActorAsync(
                    actorRef,
                    runtime.Registration.DefaultTimeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        var actorId = actor.ActorId;
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        EnsureActorContext(actor, state);
        ZLinkSpotActivation? activation = null;

        var disconnect = await state.ExecuteLockedAsync(
            () =>
        {
            if (!string.Equals(state.SessionId, stream.SessionId, StringComparison.Ordinal))
            {
                return (ShouldDisconnect: false, Activation: (ZLinkSpotActivation?)null);
            }

            state.SessionId = null;
            state.Stream = null;
            var currentActivation = state.Activation;
            state.Activation = null;

            if (currentActivation is not null && currentActivation.IsDisposed)
            {
                return (ShouldDisconnect: true, Activation: (ZLinkSpotActivation?)null);
            }

            return (ShouldDisconnect: true, Activation: currentActivation);
        },
            cancellationToken).ConfigureAwait(false);

        if (!disconnect.ShouldDisconnect)
        {
            return;
        }

        var node = getActorSpotNode();
        if (node is not null
            && state.NativeActorRef is { } actorRef
            && stream is ZLinkManagedStream managedStream)
        {
            try
            {
                await managedStream.UnbindActorAsync(
                        actor.ActorId,
                        runtime.Registration.DefaultTimeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (ZlinkException)
            {
            }
        }

        activation = disconnect.Activation;
        if (activation is not null)
        {
            await activation.DisconnectActorAsync(actor, cancellationToken).ConfigureAwait(false);
        }
        else
        {
            await actor.OnDisconnectedAsync(cancellationToken).ConfigureAwait(false);
        }

        _actorSessions.TryRemove(actorId, state);
    }

    public async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        await DispatchRouter.SubmitAsync(actor, header, body, cancellationToken)
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

using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorSessionManager(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    private readonly ZLinkActorSessionRegistry _actorSessions = new();

    public async ValueTask<CreateActorResult> CreateActorAsync(
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

        await using var scope = services.CreateAsyncScope();
        var factory = (IZLinkActorFactory)scope.ServiceProvider.GetRequiredService(factoryType);
        var actor = await factory.CreateAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        if (actor is null)
        {
            throw new InvalidOperationException($"Actor factory '{factoryType}' returned null.");
        }

        var state = _actorSessions.GetOrCreate(actor.ActorId);
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
        var state = _actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        await SubmitActorAsync(actor, header, body, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        return await SubmitActorForReplyAsync(actor, state, header, body, cancellationToken)
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
                node.LeaveActor(actorRef, currentSpotRid, runtime.Registration.DefaultTimeout);
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
            managedStream.BindActor(node, actorRef, runtime.Registration.DefaultTimeout);
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
                managedStream.UnbindActor(node, actor.ActorId, runtime.Registration.DefaultTimeout);
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
        var actorId = actor.ActorId;
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        ZLinkSpotActivation? activation;
        var shouldPrune = false;
        EnsureActorContext(actor, state);

        (activation, shouldPrune) = await state.ExecuteLockedAsync(
            () =>
        {
            var currentActivation = state.Activation;
            var prune = false;

            if (currentActivation is not null && currentActivation.IsDisposed)
            {
                state.Activation = null;
                currentActivation = null;
                prune = state.SessionId is null;
            }

            return (currentActivation, prune);
        },
            cancellationToken).ConfigureAwait(false);

        try
        {
            if (activation is null)
            {
                var previousDispatch = state.CurrentDispatch;
                state.CurrentDispatch = new ZLinkActorDispatchState(header);
                try
                {
                    await state.DispatchAsync(services, actor, header, body, cancellationToken)
                        .ConfigureAwait(false);
                }
                finally
                {
                    state.CurrentDispatch = previousDispatch;
                }

                return;
            }

            await activation.SubmitActorAsync(actor, state, header, body, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            if (shouldPrune)
            {
                _actorSessions.TryRemove(actorId, state);
            }
        }
    }

    private async ValueTask<byte[]> SubmitActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previousDispatch = state.CurrentDispatch;
        state.CurrentDispatch = new ZLinkActorDispatchState(header);
        try
        {
            return await state.DispatchForReplyAsync(services, actor, header, body, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            state.CurrentDispatch = previousDispatch;
        }
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

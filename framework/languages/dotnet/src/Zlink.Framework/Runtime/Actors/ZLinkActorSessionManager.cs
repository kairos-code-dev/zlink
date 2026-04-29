namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorSessionManager(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services)
{
    private readonly ZLinkActorSessionRegistry _actorSessions = new();

    public async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        EnsureActorContext(actor, state);
        ZLinkSpotActivation? previousActivation;

        await state.Gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            previousActivation = state.Activation;
            if (ReferenceEquals(previousActivation, activation))
            {
                return;
            }
        }
        finally
        {
            state.Gate.Release();
        }

        if (previousActivation is not null && !previousActivation.IsDisposed)
        {
            await previousActivation.LeaveActorAsync(actor, cancellationToken).ConfigureAwait(false);
        }

        await state.Gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            state.Activation = activation;
        }
        finally
        {
            state.Gate.Release();
        }
    }

    public async ValueTask LeaveActorFromSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        EnsureActorContext(actor, state);
        var shouldPrune = false;

        await state.Gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (ReferenceEquals(state.Activation, activation))
            {
                state.Activation = null;
                shouldPrune = state.SessionId is null;
            }
        }
        finally
        {
            state.Gate.Release();
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
        await state.Gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            state.SessionId = stream.SessionId;
            state.Stream = stream;
        }
        finally
        {
            state.Gate.Release();
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

        await state.Gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (!string.Equals(state.SessionId, stream.SessionId, StringComparison.Ordinal))
            {
                return;
            }

            state.SessionId = null;
            state.Stream = null;
            activation = state.Activation;
            state.Activation = null;

            if (activation is not null && activation.IsDisposed)
            {
                activation = null;
            }
        }
        finally
        {
            state.Gate.Release();
        }

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

        await state.Gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            activation = state.Activation;

            if (activation is not null && activation.IsDisposed)
            {
                state.Activation = null;
                activation = null;
                shouldPrune = state.SessionId is null;
            }
        }
        finally
        {
            state.Gate.Release();
        }

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
}

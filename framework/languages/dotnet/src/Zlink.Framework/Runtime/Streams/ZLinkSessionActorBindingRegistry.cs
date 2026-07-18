namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorBindingRegistry(ZLinkFrameworkRuntime runtime)
{
    private readonly Dictionary<string, ZLinkSessionActor> _actorsById = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkSessionActorBinding> _bindings = new(StringComparer.Ordinal);

    public IReadOnlyCollection<IZLinkSessionActor> BoundActors
    {
        get
        {
            lock (_bindings)
            {
                return _actorsById.Values.ToArray();
            }
        }
    }

    public ValueTask<IZLinkSessionActor> BindAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        var actorId = actor.ActorId;
        if (string.IsNullOrWhiteSpace(actorId)) throw new InvalidOperationException("Actor id must not be empty.");
        if (context.RoutingId is not { } sessionRid)
            throw new InvalidOperationException("Actor session binding requires a stream routing id.");

        ZLinkSessionActorBinding[] replacedBindings;
        var binding = new ZLinkSessionActorBinding(
            actorId,
            sessionRid,
            Guid.NewGuid().ToString("N"));
        var actorRef = new ZLinkSessionActor(
            context,
            actor,
            binding.SessionRid,
            binding.BindingToken);

        lock (_bindings)
        {
            replacedBindings = _bindings.Values
                .Where(current => string.Equals(current.ActorId, actorId, StringComparison.Ordinal))
                .ToArray();
            foreach (var replaced in replacedBindings)
                _bindings.Remove(BuildBindingKey(replaced.ActorId, replaced.BindingToken));

            _bindings[BuildBindingKey(actorId, binding.BindingToken)] = binding;
            _actorsById[actorId] = actorRef;
        }

        foreach (var replaced in replacedBindings)
        {
            runtime.UnbindSessionActor(replaced.ActorId, context, replaced.BindingToken);
            runtime.UnbindActorSession(replaced.ActorId, replaced.BindingToken);
        }

        runtime.BindSessionActor(actorId, context, binding.BindingToken, actorRef);
        runtime.BindActorSession(
            actorId,
            runtime.GetActorSpotNode()?.RoutingId ?? sessionRid,
            binding.SessionRid,
            binding.BindingToken);

        return ValueTask.FromResult<IZLinkSessionActor>(actorRef);
    }

    public IZLinkSessionActor? FindActor(string actorId)
    {
        if (string.IsNullOrWhiteSpace(actorId)) return null;

        lock (_bindings)
        {
            if (_actorsById.TryGetValue(actorId, out var actorRef)) return actorRef;
        }

        return null;
    }

    public ValueTask ReleaseAsync(
        ZLinkSessionContext context,
        ZLinkSessionActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (_bindings)
        {
            _bindings.Remove(BuildBindingKey(actor.ActorId, actor.BindingToken));
            if (_actorsById.TryGetValue(actor.ActorId, out var current)
                && string.Equals(current.BindingToken, actor.BindingToken, StringComparison.Ordinal))
                _actorsById.Remove(actor.ActorId);
        }

        runtime.UnbindSessionActor(actor.ActorId, context, actor.BindingToken);
        runtime.UnbindActorSession(actor.ActorId, actor.BindingToken);
        return ValueTask.CompletedTask;
    }

    public async ValueTask CleanupAsync(
        ZLinkSessionContext context,
        CancellationToken cancellationToken)
    {
        ZLinkSessionActorBinding[] bindings;
        ZLinkSessionActor[] actors;
        lock (_bindings)
        {
            bindings = _bindings.Values.ToArray();
            actors = _actorsById.Values.ToArray();
            _bindings.Clear();
            _actorsById.Clear();
        }

        // The session transport is gone: propagate the disconnect to each
        // remotely-bound actor BEFORE dropping the local records — the remote
        // relay resolves the actor's session route from them. A node-local
        // actor's disconnect callback rides the native stream binding instead.
        var localNodeRid = runtime.GetActorSpotNode()?.RoutingId;
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine(
                $"[session-cleanup] actors={actors.Length} localNode={localNodeRid}");
        foreach (var actor in actors)
        {
            if (localNodeRid is { } local && actor.Ref.NodeRid == local) continue;
            try
            {
                await runtime.NotifyActorDisconnectedAsync(
                        actor.Ref,
                        actor.BindingToken,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception error)
            {
                // Cleanup must release every binding even when one peer is
                // unreachable; the actor node's own lifecycle recovers it.
                if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                    Console.Error.WriteLine(
                        $"[session-cleanup] notify failed actor={actor.ActorId}: {error.Message}");
            }
        }

        foreach (var binding in bindings)
        {
            runtime.UnbindSessionActor(binding.ActorId, context, binding.BindingToken);
            runtime.UnbindActorSession(binding.ActorId, binding.BindingToken);
        }
    }

    private static string BuildBindingKey(string actorId, string bindingToken)
    {
        return $"{actorId}\0{bindingToken}";
    }
}

internal readonly record struct ZLinkSessionActorBinding(
    string ActorId,
    RoutingId SessionRid,
    string BindingToken);

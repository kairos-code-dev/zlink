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

        // The transport disconnect owns one fixed binding snapshot. Notify
        // every exact binding concurrently, but bound each callback by the
        // runtime deadline so one Actor cannot hold session cleanup.
        await Task.WhenAll(actors
                .DistinctBy(actor => (
                    actor.ActorId,
                    actor.Ref.NodeRid,
                    actor.Ref.Generation,
                    actor.BindingToken))
                .Select(NotifyBestEffortAsync))
            .ConfigureAwait(false);

        // Tombstones are always removed, including callback failure, timeout,
        // and a concurrent explicit notification of the same binding.
        foreach (var binding in bindings)
        {
            runtime.UnbindSessionActor(binding.ActorId, context, binding.BindingToken);
            runtime.UnbindActorSession(binding.ActorId, binding.BindingToken);
        }

        return;

        async Task NotifyBestEffortAsync(ZLinkSessionActor actor)
        {
            try
            {
                await actor.NotifyDisconnectedAsync(cancellationToken)
                    .AsTask()
                    .WaitAsync(runtime.Registration.DefaultRequestTimeout, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception)
            {
                // Cleanup is all-settled. The exact binding token prevents a
                // stale or duplicate notification from affecting a replacement.
            }
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

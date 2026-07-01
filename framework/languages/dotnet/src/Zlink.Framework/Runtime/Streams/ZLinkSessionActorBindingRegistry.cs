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

        var binding = new ZLinkSessionActorBinding(
            actorId,
            sessionRid,
            Guid.NewGuid().ToString("N"));
        var actorRef = new ZLinkSessionActor(
            context,
            actor,
            binding.SessionRid,
            binding.BindingToken);

        runtime.BindSessionActor(actorId, context, binding.BindingToken, actorRef);
        runtime.BindActorSession(
            actorId,
            sessionRid,
            binding.SessionRid,
            binding.BindingToken);

        lock (_bindings)
        {
            _bindings[BuildBindingKey(actorId, binding.BindingToken)] = binding;
            _actorsById[actorId] = actorRef;
        }

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

    public ValueTask CleanupAsync(
        ZLinkSessionContext context,
        CancellationToken cancellationToken)
    {
        ZLinkSessionActorBinding[] bindings;
        lock (_bindings)
        {
            bindings = _bindings.Values.ToArray();
            _bindings.Clear();
            _actorsById.Clear();
        }

        foreach (var binding in bindings)
        {
            runtime.UnbindSessionActor(binding.ActorId, context, binding.BindingToken);
            runtime.UnbindActorSession(binding.ActorId, binding.BindingToken);
        }

        return ValueTask.CompletedTask;
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

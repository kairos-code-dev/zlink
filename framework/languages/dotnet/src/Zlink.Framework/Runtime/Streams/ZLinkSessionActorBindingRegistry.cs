namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorBindingRegistry(ZLinkFrameworkRuntime runtime)
{
    private readonly Dictionary<string, ZLinkSessionActorBinding> _bindings = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkActorRef> _actorsById = new(StringComparer.Ordinal);

    public IReadOnlyCollection<IZLinkActorRef> BoundActors
    {
        get
        {
            lock (_bindings)
            {
                return _actorsById.Values.ToArray();
            }
        }
    }

    public ValueTask<IZLinkActorRef> BindAsync(
        ZLinkSessionContext context,
        string actorId,
        string actorType,
        ZLinkActorRemoteAddress remoteAddress,
        bool isRemote,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Actor id must not be empty.");
        }
        if (context.RoutingId is not { } sessionRid)
        {
            throw new InvalidOperationException("Actor session binding requires a stream routing id.");
        }

        var binding = new ZLinkSessionActorBinding(
            actorId,
            sessionRid,
            Guid.NewGuid().ToString("N"));
        var actorRef = new ZLinkActorRef(
            actorId,
            actorType,
            remoteAddress,
            isRemote,
            binding.SessionRid,
            binding.BindingToken);

        runtime.BindSessionActor(actorId, context, binding.BindingToken, actorRef);
        runtime.BindActorSession(actorId, binding.SessionRid, binding.BindingToken);

        lock (_bindings)
        {
            _bindings[BuildBindingKey(actorId, binding.BindingToken)] = binding;
            _actorsById[actorId] = actorRef;
        }

        return ValueTask.FromResult<IZLinkActorRef>(actorRef);
    }

    public bool TryGetBoundActor(
        string actorId,
        out IZLinkActorRef actor)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            actor = null!;
            return false;
        }

        lock (_bindings)
        {
            if (_actorsById.TryGetValue(actorId, out var actorRef))
            {
                actor = actorRef;
                return true;
            }
        }

        actor = null!;
        return false;
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

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorBindingRegistry(ZLinkFrameworkRuntime runtime)
{
    private readonly Dictionary<string, ZLinkSessionActorBinding> _bindings = new(StringComparer.Ordinal);

    public ValueTask<IZLinkActorRef> BindAsync(
        ZLinkSessionContext context,
        string sessionId,
        string actorId,
        string actorType,
        string routerChannelId,
        RoutingId actorNodeRid,
        ulong actorGeneration,
        bool isRemote,
        Func<ZLinkActorRef, CancellationToken, ValueTask> notifyDisconnectedAsync,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Actor id must not be empty.");
        }

        var sessionRouterId = runtime.ResolveSessionRouterId(routerChannelId);
        var binding = new ZLinkSessionActorBinding(
            actorId,
            sessionRouterId,
            Guid.NewGuid().ToString("N"));
        var actorRef = new ZLinkActorRef(
            actorId,
            actorType,
            new ZLinkActorRemoteAddressState(new ZLinkActorRemoteAddress(
                routerChannelId,
                actorNodeRid,
                actorGeneration)),
            binding.SessionRouterId,
            isRemote,
            binding.BindingToken,
            notifyDisconnectedAsync);

        runtime.BindSessionActor(actorId, context, binding.BindingToken, actorRef);
        runtime.BindActorSession(actorId, binding.SessionRouterId, binding.BindingToken);

        lock (_bindings)
        {
            _bindings[BuildBindingKey(actorId, binding.BindingToken)] = binding;
        }

        return ValueTask.FromResult<IZLinkActorRef>(actorRef);
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
    RoutingId SessionRouterId,
    string BindingToken);

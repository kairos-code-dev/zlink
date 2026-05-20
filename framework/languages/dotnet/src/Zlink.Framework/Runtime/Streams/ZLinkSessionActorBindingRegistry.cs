using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorBindingRegistry(ZLinkFrameworkRuntime runtime)
{
    private readonly Dictionary<string, ZLinkActorSessionBinding> _bindings = new(StringComparer.Ordinal);

    public async ValueTask<IZLinkActorRef> BindAsync(
        ZLinkSessionContext context,
        string sessionId,
        string actorId,
        string actorType,
        string routerChannelId,
        RoutingId actorNodeRid,
        ulong actorGeneration,
        Func<ZLinkActorRef, CancellationToken, ValueTask> notifyDisconnectedAsync,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Actor id must not be empty.");
        }

        var sessionRouterId = runtime.ResolveSessionRouterId(routerChannelId);
        var binding = new ZLinkActorSessionBinding(
            actorId,
            sessionRouterId,
            Guid.NewGuid().ToString("N"));
        var actorRef = new ZLinkActorRef(
            actorId,
            actorType,
            new ZLinkActorRouteState(new ZLinkActorRoute(
                routerChannelId,
                actorNodeRid,
                actorGeneration)),
            notifyDisconnectedAsync);

        var store = runtime.Services.GetRequiredService<IZLinkActorSessionBindingStore>();
        runtime.BindSessionActor(actorId, context, binding.BindingToken, actorRef);
        try
        {
            await store.BindSessionAsync(binding, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            runtime.UnbindSessionActor(actorId, context, binding.BindingToken);
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SessionLocationUpdateFailed,
                $"Failed to bind session location for actor '{actorId}'.",
                false,
                ex);
        }

        lock (_bindings)
        {
            _bindings[BuildBindingKey(actorId, binding.BindingToken)] = binding;
        }

        return actorRef;
    }

    public async ValueTask CleanupAsync(
        ZLinkSessionContext context,
        CancellationToken cancellationToken)
    {
        ZLinkActorSessionBinding[] bindings;
        lock (_bindings)
        {
            bindings = _bindings.Values.ToArray();
            _bindings.Clear();
        }

        foreach (var binding in bindings)
        {
            runtime.UnbindSessionActor(binding.ActorId, context, binding.BindingToken);
            var store = runtime.Services.GetService<IZLinkActorSessionBindingStore>();
            if (store is not null)
            {
                await store.UnbindSessionAsync(
                    new ZLinkActorSessionUnbind(
                        binding.ActorId,
                        binding.BindingToken),
                    cancellationToken).ConfigureAwait(false);
            }
        }
    }

    private static string BuildBindingKey(string actorId, string bindingToken)
    {
        return $"{actorId}\0{bindingToken}";
    }
}

using Zlink;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Infrastructure;

internal sealed class RegistryActorSessionLocationStore(
    IRegistryDiscoveryMetadata registry,
    string metadataNamespace)
    : IZLinkActorSessionLocationWriter,
      IZLinkActorSessionRouteResolver
{
    public ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken)
    {
        return registry.PutAsync(
            Key(binding.ActorId),
            new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["routerChannelId"] = binding.RouterChannelId,
                ["sessionRouterId"] = binding.SessionRouterId.ToHex(),
                ["sessionId"] = binding.SessionId,
                ["bindingToken"] = binding.BindingToken,
            },
            cancellationToken);
    }

    public ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken)
    {
        return registry.DeleteIfAsync(
            Key(binding.ActorId),
            new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["sessionId"] = binding.SessionId,
                ["bindingToken"] = binding.BindingToken,
            },
            cancellationToken);
    }

    public async ValueTask<ZLinkActorSessionRoute> ResolveSessionRouteAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var entry = await registry.GetRequiredAsync(
            Key(actorId),
            cancellationToken).ConfigureAwait(false);

        return new ZLinkActorSessionRoute(
            entry.Require("routerChannelId"),
            RoutingId.FromString(entry.Require("sessionRouterId")),
            entry.Require("sessionId"),
            entry.Require("bindingToken"));
    }

    private string Key(string actorId)
    {
        return $"{metadataNamespace}/actor-session/{actorId}";
    }
}

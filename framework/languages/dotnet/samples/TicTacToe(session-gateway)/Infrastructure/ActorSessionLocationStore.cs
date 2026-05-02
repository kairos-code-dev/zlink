using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionGateway.Infrastructure;

public sealed class RegistryActorSessionLocationStore(
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
                ["sessionRouterId"] = binding.SessionRouterId.ToHex(),
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
            RoutingId.FromString(entry.Require("sessionRouterId")),
            entry.Require("bindingToken"));
    }

    private string Key(string actorId)
    {
        return $"{metadataNamespace}/actor-session/{actorId}";
    }
}

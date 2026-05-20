using TicTacToe.SessionActorDispatch.Infrastructure;
using Systems.Zlink;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionGateway.Infrastructure;

public sealed class RegistryActorSessionLocationStore(
    IRegistryDiscoveryMetadata registry,
    string metadataNamespace)
    : IZLinkActorSessionBindingStore
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

    public async ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
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

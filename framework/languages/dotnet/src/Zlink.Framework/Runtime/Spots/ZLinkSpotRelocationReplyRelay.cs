namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkRelocationReplyTarget(
    ZLinkFrameworkRuntime runtime,
    IZLinkAuthorityStore authorityStore) : IRelocationReplyRelayTarget
{
    public async ValueTask<ZLinkServiceWireCodec.ReplyRelayAckRecord?> RelayAsync(
        ZLinkServiceWireCodec.ReplyRelayRecord relay,
        RoutingId sourceNodeRid,
        IReadOnlyList<Message> payload,
        CancellationToken cancellationToken)
    {
        var consumed = false;
        try
        {
            if (!runtime.SpotRelocationReplyRoutes.TryGetRelayBinding(
                    relay,
                    sourceNodeRid,
                    out var binding))
                return null;
            var authority = await authorityStore.ReadAuthorityAsync(
                    binding.AuthorityKey,
                    cancellationToken)
                .ConfigureAwait(false);
            if (authority is not ZLinkAuthorityReadResult.Found found
                || !StringComparer.Ordinal.Equals(
                    found.Snapshot.StoreVersion,
                    relay.Coordinator.ExpectedAuthorityStoreVersion))
                return null;

            var result = runtime.SpotRelocationReplyRoutes.TryRelay(
                relay,
                sourceNodeRid,
                payload,
                SendFlags.None,
                out consumed,
                out var alreadyTerminal);
            if (result != SubmitResult.Ok)
                return null;
            return new ZLinkServiceWireCodec.ReplyRelayAckRecord(
                relay.RelocationId,
                relay.Coordinator,
                relay.OperationId,
                binding.RequestSource,
                alreadyTerminal ? (byte)2 : (byte)1);
        }
        finally
        {
            if (!consumed)
                ZLinkMessageParts.DisposeAll(payload);
        }
    }
}

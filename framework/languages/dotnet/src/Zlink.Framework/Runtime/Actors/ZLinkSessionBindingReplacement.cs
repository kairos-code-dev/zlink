namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkSessionBindingReplacement
{
    internal static async ValueTask CompletePreviousAsync(
        string actorId,
        RoutingId targetNodeRid,
        ZLinkRemoteSessionPreviousBinding? previous,
        Func<ZLinkRemoteSessionUnbindRequest, CancellationToken,
            ValueTask<ZLinkRemoteSessionUnbindResponse>> sendTombstoneAsync,
        CancellationToken cancellationToken)
    {
        if (previous is null
            || RoutingId.From(previous.TargetNodeRid) == targetNodeRid)
            return;

        var response = await sendTombstoneAsync(
                new ZLinkRemoteSessionUnbindRequest(
                    actorId,
                    previous.TargetNodeRid,
                    previous.BindingToken,
                    previous.BindingGeneration,
                    previous.ObjectGeneration,
                    previous.MeshName,
                    previous.TargetNodeGeneration,
                    previous.AuthorityOwnerGeneration,
                    previous.OwnerLeaseGeneration,
                    previous.SessionOwnerNodeGeneration),
                cancellationToken)
            .ConfigureAwait(false);
        if (!response.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{actorId}' previous session binding was not invalidated.",
                true);
    }
}

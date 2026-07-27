using Systems.Zlink;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;

namespace AutomaticTurnDispatch.Server.Session.Support;

internal sealed partial class AwaitSession
{
    private async Task<TRes> RequestPlayControlAsync<TRes>(
        IZLinkRouteClient routes,
        object request,
        CancellationToken cancellationToken)
    {
        return await RequestPlayControlAsync<TRes>(
            routes,
            request,
            "play-a",
            cancellationToken);
    }

    private async Task<TRes> RequestPlayControlAsync<TRes>(
        IZLinkRouteClient routes,
        object request,
        string target,
        CancellationToken cancellationToken)
        => await routes.RequestToNode(
                AutomaticTurnDispatchNames.ControlChannel,
                ResolveControlNode(target),
                request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<TRes>(cancellationToken);

    private static async Task SendSpotAsync(
        IZLinkSpotClient spotsClient,
        string spotId,
        object message,
        CancellationToken cancellationToken)
    {
        await spotsClient.SendToSpot(spotId, message).Async(cancellationToken);
    }

    private static async ValueTask<TRes> RequestSpotAsync<TRes>(
        IZLinkSpotClient spotsClient,
        string spotId,
        object request,
        CancellationToken cancellationToken)
    {
        return await spotsClient.RequestToSpot(spotId, request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<TRes>(cancellationToken);
    }

    private static string TargetOrDefault(ZLinkSessionDispatchContext dispatch)
    {
        var target = dispatch.Metadata.Find(AutomaticTurnDispatchNames.TargetNodeRidMetadata);
        return string.IsNullOrWhiteSpace(target)
            ? "play-a"
            : target;
    }

    private RoutingId ResolveControlNode(string targetPrefix)
    {
        var peers = meshRuntime.Snapshot(AutomaticTurnDispatchNames.ControlChannel).Peers;
        foreach (var peer in peers)
        {
            if (!peer.Ready)
                continue;
            var rid = peer.Rid.ToString();
            if (string.Equals(rid, targetPrefix, StringComparison.Ordinal)
                || rid.StartsWith($"{targetPrefix}-", StringComparison.Ordinal))
                return peer.Rid;
        }

        throw new InvalidOperationException(
            $"Ready control MeshNode with prefix '{targetPrefix}' was not found.");
    }

    private static void AssertOrder(string[] evidence, string requestFilter, string[] markers)
    {
        var cursor = -1;
        foreach (var marker in markers)
        {
            var index = Array.FindIndex(
                evidence,
                cursor + 1,
                line => line.Contains(requestFilter, StringComparison.Ordinal)
                        && line.Contains(marker, StringComparison.Ordinal));
            if (index < 0)
                throw new InvalidOperationException($"Missing ordered marker '{marker}' for {requestFilter}.");

            cursor = index;
        }
    }

}

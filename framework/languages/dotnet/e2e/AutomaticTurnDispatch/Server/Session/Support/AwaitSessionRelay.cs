using Systems.Zlink;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;

namespace AutomaticTurnDispatch.Server.Session.Support;

internal sealed partial class AwaitSession
{
    private static async Task<TRes> RequestPlayControlAsync<TRes>(
        IZLinkRouteClient routes,
        object request,
        CancellationToken cancellationToken)
    {
        return await RequestPlayControlAsync<TRes>(
            routes,
            request,
            RoutingId.From("play-a"),
            cancellationToken);
    }

    private static async Task<TRes> RequestPlayControlAsync<TRes>(
        IZLinkRouteClient routes,
        object request,
        RoutingId target,
        CancellationToken cancellationToken)
        => await routes.RequestToNode(
                AutomaticTurnDispatchNames.ControlChannel,
                target,
                request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<TRes>(cancellationToken);

    private static async Task SendSpotAsync(
        IZLinkSpotClient spotsClient,
        IZLinkSpotHandleResolver spots,
        string spotRid,
        object message,
        CancellationToken cancellationToken)
    {
        var handle = await spots.ResolveSpotHandleAsync(
                         AutomaticTurnDispatchNames.SpotChannel,
                         RoutingId.From(spotRid),
                         cancellationToken)
                     ?? throw new ZLinkFrameworkException(
                         ZLinkFrameworkErrorKind.SpotRouteNotFound,
                         $"Spot '{spotRid}' has no live location row.");
        await spotsClient.SendToSpot(handle, message).SubmitAsync(cancellationToken);
    }

    private static async ValueTask<TRes> RequestSpotAsync<TRes>(
        IZLinkSpotClient spotsClient,
        IZLinkSpotHandleResolver spots,
        string spotRid,
        object request,
        CancellationToken cancellationToken)
    {
        var handle = await spots.ResolveSpotHandleAsync(
                         AutomaticTurnDispatchNames.SpotChannel,
                         RoutingId.From(spotRid),
                         cancellationToken)
                     ?? throw new ZLinkFrameworkException(
                         ZLinkFrameworkErrorKind.SpotRouteNotFound,
                         $"Spot '{spotRid}' has no live location row.");
        return await spotsClient.RequestToSpot(handle, request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<TRes>(cancellationToken);
    }

    private static RoutingId TargetOrDefault(ZLinkSessionDispatchContext dispatch)
    {
        var target = dispatch.Metadata.Find(AutomaticTurnDispatchNames.TargetNodeRidMetadata);
        return string.IsNullOrWhiteSpace(target)
            ? RoutingId.From("play-a")
            : RoutingId.From(target);
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

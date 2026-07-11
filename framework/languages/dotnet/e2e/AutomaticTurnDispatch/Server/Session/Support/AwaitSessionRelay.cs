using Systems.Zlink;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Streams;

namespace AutomaticTurnDispatch.Server.Session.Support;

internal sealed partial class AwaitSession
{
    private static async Task<TRes> RequestPlayControlWithRetryAsync<TRes>(
        IZLinkRouteClient routes,
        object request,
        string packetName,
        CancellationToken cancellationToken)
    {
        return await RequestPlayControlWithRetryAsync<TRes>(
            routes,
            request,
            packetName,
            RoutingId.From("play-a"),
            cancellationToken);
    }

    private static async Task<TRes> RequestPlayControlWithRetryAsync<TRes>(
        IZLinkRouteClient routes,
        object request,
        string packetName,
        RoutingId target,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
            try
            {
                return await routes.RequestToNode(
                        AutomaticTurnDispatchNames.ControlChannel,
                        target,
                        request)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<TRes>(cancellationToken);
            }
            catch (Exception ex) when (
                ex is TimeoutException or ZlinkRequestException or ZlinkSubmitException
                || ex is ZLinkFrameworkException { InnerException: ZlinkRequestException or ZlinkSubmitException })
            {
                last = ex;
                await Task.Delay(100, cancellationToken);
            }

        throw new TimeoutException($"Timed out requesting play control packet '{packetName}'.", last);
    }

    private static async Task SendSpotWithRetryAsync(
        IZLinkRouteClient routes,
        IZLinkSpotHandleResolver spots,
        string spotRid,
        object message,
        string packetName,
        CancellationToken cancellationToken)
    {
        // Resolve once per attempt and message with the address; a failed
        // attempt re-resolves (spot-address messaging draft §7).
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
            try
            {
                var address = await spots.ResolveSpotHandleAsync(
                                  RoutingId.From(spotRid), cancellationToken)
                              ?? throw new ZLinkFrameworkException(
                                  ZLinkFrameworkErrorKind.SpotRouteNotFound,
                                  $"Spot '{spotRid}' has no live address.");
                routes.SendToSpot(address,
                        message).Submit(cancellationToken);
                return;
            }
            catch (Exception ex) when (
                ex is TimeoutException or ZLinkFrameworkException
                || ex is ZlinkRequestException or ZlinkSubmitException)
            {
                last = ex;
                await Task.Delay(100, cancellationToken);
            }

        throw new TimeoutException($"Timed out sending spot '{spotRid}' packet '{packetName}'.", last);
    }

    private static async ValueTask<TRes> RequestSpotWithRetryAsync<TRes>(
        IZLinkRouteClient routes,
        IZLinkSpotHandleResolver spots,
        string spotRid,
        object request,
        string packetName,
        CancellationToken cancellationToken)
    {
        // Resolve once per attempt and message with the address; a stale
        // address fails typed and the next attempt re-resolves (draft §7).
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
            try
            {
                var address = await spots.ResolveSpotHandleAsync(
                                  RoutingId.From(spotRid), cancellationToken)
                              ?? throw new ZLinkFrameworkException(
                                  ZLinkFrameworkErrorKind.SpotRouteNotFound,
                                  $"Spot '{spotRid}' has no live address.");
                return await routes.RequestToSpot(address,
                        request)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<TRes>(cancellationToken);
            }
            catch (Exception ex) when (
                ex is TimeoutException or ZLinkFrameworkException
                || ex is ZlinkRequestException or ZlinkSubmitException)
            {
                last = ex;
                await Task.Delay(100, cancellationToken);
            }

        throw new TimeoutException($"Timed out requesting spot '{spotRid}' packet '{packetName}'.", last);
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

    private static void Ensure(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
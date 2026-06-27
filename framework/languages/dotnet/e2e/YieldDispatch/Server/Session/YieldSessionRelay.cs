using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Actors;

namespace YieldDispatch.Server.Session;

internal sealed partial class YieldSession
{
    private static async Task<TReply> RequestPlayControlWithRetryAsync<TReply>(
        IZLinkRouteClient routes,
        object request,
        string packetName,
        CancellationToken cancellationToken)
        => await RequestPlayControlWithRetryAsync<TReply>(
            routes,
            request,
            packetName,
            RoutingId.From("play-a"),
            cancellationToken);

    private static async Task<TReply> RequestPlayControlWithRetryAsync<TReply>(
        IZLinkRouteClient routes,
        object request,
        string packetName,
        RoutingId target,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await routes.Request(
                        YieldDispatchNames.ControlChannel,
                        target,
                        request)
                    .PacketName(packetName)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<TReply>(cancellationToken);
            }
            catch (Exception ex) when (
                ex is TimeoutException or ZlinkRequestException or ZlinkSubmitException
                || ex is ZLinkFrameworkException { InnerException: ZlinkRequestException or ZlinkSubmitException })
            {
                last = ex;
                await Task.Delay(100, cancellationToken);
            }
        }

        throw new TimeoutException($"Timed out requesting play control packet '{packetName}'.", last);
    }

    private static async Task SendSpotWithRetryAsync(
        IZLinkRouteClient routes,
        string spotRid,
        object message,
        string packetName,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                await routes.Send(
                        YieldDispatchNames.SpotRouteChannel,
                        RoutingId.From(spotRid),
                        message)
                    .PacketName(packetName)
                    .Async(cancellationToken);
                return;
            }
            catch (Exception ex) when (
                ex is TimeoutException or ZLinkFrameworkException
                || ex is ZlinkRequestException or ZlinkSubmitException)
            {
                last = ex;
                await Task.Delay(100, cancellationToken);
            }
        }

        throw new TimeoutException($"Timed out sending spot '{spotRid}' packet '{packetName}'.", last);
    }

    private static async ValueTask<TReply> RequestSpotWithRetryAsync<TReply>(
        IZLinkRouteClient routes,
        string spotRid,
        object request,
        string packetName,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await routes.Request(
                        YieldDispatchNames.SpotRouteChannel,
                        RoutingId.From(spotRid),
                        request)
                    .PacketName(packetName)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<TReply>(cancellationToken);
            }
            catch (Exception ex) when (
                ex is TimeoutException or ZLinkFrameworkException
                || ex is ZlinkRequestException or ZlinkSubmitException)
            {
                last = ex;
                await Task.Delay(100, cancellationToken);
            }
        }

        throw new TimeoutException($"Timed out requesting spot '{spotRid}' packet '{packetName}'.", last);
    }

    private static RoutingId TargetOrDefault(ZLinkSessionDispatchContext dispatch)
    {
        var target = dispatch.Metadata.Find(YieldDispatchNames.TargetNodeRidMetadata);
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
            {
                throw new InvalidOperationException($"Missing ordered marker '{marker}' for {requestFilter}.");
            }

            cursor = index;
        }
    }

    private static void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}

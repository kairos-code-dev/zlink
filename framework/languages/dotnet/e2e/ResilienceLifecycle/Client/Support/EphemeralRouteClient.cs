using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ResilienceLifecycle.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Locations.Redis;

namespace ResilienceLifecycle.Client.Support;

internal static class EphemeralRouteClient
{
    // A retiring host publishes drain state for one polling interval plus
    // the bounded location-store read window before it removes its row. The
    // next host can therefore need more than the five-second request timeout
    // to become visible even though its eventual request still uses that
    // stricter timeout.
    private static readonly TimeSpan PeerDiscoveryDeadline = TimeSpan.FromSeconds(15);

    public static async Task<ProfileRes> RequestAsync(
        ClientOptions options,
        ProfileReq request,
        CancellationToken cancellationToken = default)
    {
        using var host = Host.CreateDefaultBuilder()
            .ConfigureAppConfiguration(static configuration =>
                configuration.Sources.Clear())
            .ConfigureLogging(static logging => logging.ClearProviders())
            .ConfigureServices(services =>
            {
                services.AddZLinkFramework(framework =>
                {
                    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                        .SetConnectionString(options.RedisEndpoint)
                        .SetKeyPrefix(options.RedisKeyPrefix)));
                    framework.ConfigureDispatch()
                        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                        .TraceLogFile(Path.Combine(
                            options.LogDir,
                            $"ephemeral-{request.Marker}-flow.log"))
                        .TraceLabel($"ephemeral-{request.Marker}");
                    var mesh = framework.AddRouteMesh(ResilienceLifecycleNames.Channel)
                        .Listen("tcp://127.0.0.1:0")
                        .UseAllocatedRoutingId(128, "ephemeral")
                        .SetRoutingIdAllocationGroup("resilience.ephemeral");
                    mesh.ChannelName(ResilienceLifecycleNames.ConsumerChannel);
                });
            })
            .Build();

        await host.StartAsync(cancellationToken);
        try
        {
            var runtime = host.Services.GetRequiredService<IZLinkRouteMeshRuntime>();
            var locations = host.Services.GetRequiredService<IZLinkLocationRuntimeQuery>();
            await WaitForReadyPeerAsync(runtime, locations, cancellationToken);
            var client = host.Services.GetRequiredService<IZLinkRouteClient>();
            return await client.RequestToChannel(
                    ResilienceLifecycleNames.Channel,
                    ResilienceLifecycleNames.Channel,
                    request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ProfileRes>(cancellationToken);
        }
        finally
        {
            await host.StopAsync(CancellationToken.None);
        }
    }

    private static async Task WaitForReadyPeerAsync(
        IZLinkRouteMeshRuntime runtime,
        IZLinkLocationRuntimeQuery locations,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + PeerDiscoveryDeadline;
        string? lastSnapshot = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = runtime.Snapshot(ResilienceLifecycleNames.Channel);
            lastSnapshot = string.Join(
                ";",
                snapshot.Peers.Select(peer =>
                    $"{peer.Rid}:ready={peer.Ready}:channels=[{string.Join(",", peer.ChannelNames)}]"))
                + "|local="
                + string.Join(
                    ";",
                    snapshot.Channels.Select(channel =>
                        $"{channel.ChannelName}:ready={channel.ReadyMemberCount}:selectable={channel.Selectable}"));
            if (snapshot.Peers.Any(peer =>
                    peer.Ready
                    && peer.ChannelNames.Contains(
                        ResilienceLifecycleNames.Channel,
                        StringComparer.Ordinal)))
                return;
            await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
        }

        var descriptors = await locations.ListMeshNodeDescriptorsAsync(
            ResilienceLifecycleNames.Channel,
            cancellationToken);
        var descriptorText = string.Join(
            ";",
            descriptors.Select(descriptor =>
                $"{descriptor.Rid}@{descriptor.Endpoint}:generation={descriptor.LifecycleGeneration}"));
        throw new TimeoutException(
            "Ephemeral client did not observe a selectable provider: "
            + $"{lastSnapshot}|descriptors={descriptorText}.");
    }
}

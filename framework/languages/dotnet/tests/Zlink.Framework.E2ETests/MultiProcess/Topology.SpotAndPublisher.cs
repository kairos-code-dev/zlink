using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Streams;
using Zlink.Framework.Tests.Common;

namespace Zlink.Framework.E2ETests.MultiProcess;

public sealed partial class TopologyTests
{
    [Fact]
    public async Task SpotMonitoring_Emits_Peers_And_Subjects_For_Remote_Process_Node()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var localNodeEndpoint = GetFreeTcpEndpoint();
        var remoteNodeEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.stage.{Guid.NewGuid():N}";

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotChangeProbe>();
        builder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkSpotEvent>>(static provider =>
            provider.GetRequiredService<SpotChangeProbe>());
        builder.Services.AddScoped<LocalMonitoringStageSubscriptionHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
                mesh.AddNode("local-stage-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.SetPubBind(localNodeEndpoint);
                });
                spot.AddSpotFactory<LocalMonitoringStageSpot>();
            });
            });
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSpotEvents("local-stage-node", TimeSpan.FromMilliseconds(100));
        });

        using var host = builder.Build();
        await host.StartAsync(timeout.Token);

        await using var remoteHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "spot-node",
            "--discovery-channel", spotChannel,
            "--discovery-endpoint", registryRouterEndpoint,
            "--spot-node-name", "remote-stage-node",
            "--spot-bind-endpoint", remoteNodeEndpoint,
            "--enable-pubsub",
            "--spot-factory", "stage",
            "--create-spot", "stage");

        var probe = host.Services.GetRequiredService<SpotChangeProbe>();
        await probe.WaitForPeerAsync(TimeSpan.FromSeconds(15));
        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        _ = await manager.CreateAsync<LocalMonitoringStageSpot>(timeout.Token);
        await probe.WaitForSubjectAsync(TimeSpan.FromSeconds(15));

        await host.StopAsync(timeout.Token);
    }

    [Fact]
    public async Task ChannelSubscriber_DiscoveryAttach_Receives_RemotePublish_From_TestHostProcesses()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var publisherEndpoint = GetFreeTcpEndpoint();
        var eventFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-channel-event-{Guid.NewGuid():N}.log");

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);
        await using var subscriberHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-subscriber",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--event-file", eventFilePath);
        await using var publisherHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-publisher",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--publisher-endpoint", publisherEndpoint,
            "--publish-topic", "profile.cache-invalidated",
            "--publish-value", "remote-channel");

        var line = await WaitForFileLineAsync(
            eventFilePath,
            static value => value.Contains("profile.cache-invalidated:remote-channel", StringComparison.Ordinal),
            TimeSpan.FromSeconds(15));

        Assert.Contains("profile.cache-invalidated:remote-channel", line, StringComparison.Ordinal);
    }

    [Fact]
    public async Task OutboundOnly_SpotPublisherClient_Publishes_Across_TestHostProcesses()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.stage.{Guid.NewGuid():N}";
        var eventFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-spot-event-{Guid.NewGuid():N}.log");

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);
        await using var subscriberHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "spot-node",
            "--discovery-channel", spotChannel,
            "--discovery-endpoint", registryRouterEndpoint,
            "--spot-node-name", "subscriber-node",
            "--spot-bind-endpoint", subscriberNodeEndpoint,
            "--enable-pubsub",
            "--spot-factory", "stage",
            "--create-spot", "stage",
            "--event-file", eventFilePath);
        await using var publisherHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "spot-node",
            "--discovery-channel", spotChannel,
            "--discovery-endpoint", registryRouterEndpoint,
            "--spot-node-name", "publisher-node",
            "--spot-bind-endpoint", publisherNodeEndpoint,
            "--enable-pubsub",
            "--attach-spot-publisher-channel", spotChannel,
            "--publish-topic", "stage.monitor",
            "--publish-value", "remote-spot");

        var line = await WaitForFileLineAsync(
            eventFilePath,
            static value => value.Contains("remote-spot", StringComparison.Ordinal),
            TimeSpan.FromSeconds(15));

        Assert.Contains("remote-spot", line, StringComparison.Ordinal);
    }
}

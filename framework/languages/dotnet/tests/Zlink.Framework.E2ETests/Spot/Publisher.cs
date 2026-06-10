using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;


public sealed class PublisherTests : SpotTestSupport
{
    [Fact]
    public async Task OutboundOnly_SpotPublisherClient_Publishes_To_TargetChannel()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        subscriberBuilder.Services.AddScoped<ExternalStageEventHandler>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(registryRouterEndpoint));
                mesh.AddNode("subscriber-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.BindPubSub(subscriberNodeEndpoint);
                });
                spot.AddSpotFactory<ExternalSubscriberStageSpot>();
            });
            });
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(registryRouterEndpoint));
                mesh.AddNode("publisher-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.BindPubSub(publisherNodeEndpoint);
                });
                spot.AttachSpotPublisherClient("game.stage");
            });
            });
        });

        using var registryHost = registryBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await registryHost.StartAsync();
        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var manager = subscriberHost.Services.GetRequiredService<IZLinkSpotManager>();
        _ = await manager.CreateAsync<ExternalSubscriberStageSpot>();

        var publisher = publisherHost.Services.GetRequiredService<IZLinkSpotPublisherClient>();
        var recorder = subscriberHost.Services.GetRequiredService<SpotLifecycleRecorder>();
        var publisherRuntime = publisherHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var subscriberRuntime = subscriberHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();

        await RetryAsync(
            async () => (await manager.ListAsync()).Count,
            count => count == 1,
            TimeSpan.FromSeconds(10));

        try
        {
            await RetryAsync(
                async () =>
	                {
	                    await publisher.PublishSpot(
	                            "game.stage",
	                            "stage.external",
	                            new ExternalStageEvent("external"))
	                        .SubmitAsync();
	                    await Task.Yield();
	                    return recorder.ExternalEvents.Count;
	                },
                count => count > 0,
                TimeSpan.FromSeconds(10));
        }
        catch (Exception ex) when (ex is TimeoutException or ZlinkException)
        {
            var publisherSnapshot = publisherRuntime.GetSpotMonitoringSnapshot("publisher-node");
            var subscriberSnapshot = subscriberRuntime.GetSpotMonitoringSnapshot("subscriber-node");
            var publisherBundle = publisherRuntime.GetSpotPublisherBundle("game.stage");
            var directPublishResult = false;
            var rawReceived = "probe-unavailable";
            var probeParts = ZLinkEnvelopeCodec.EncodeParts(
                new ZLinkEnvelopeHeader(
                    ZLinkMessageKind.Publish,
                    "game.stage",
                    nameof(ExternalStageEvent),
                    ZLinkEnvelopeCodec.DefaultContentType,
                    null,
                    null,
                    "stage.external",
                    null,
                    null),
                new ExternalStageEvent("probe"),
                typeof(ExternalStageEvent));
            try
            {
                directPublishResult = publisherBundle.Spot.Publish(
                    "stage.external",
                    probeParts,
                    global::Systems.Zlink.SendFlags.None);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(probeParts);
            }

            var subscriberActivation = GetSingleSpotActivation(subscriberRuntime, "subscriber-node");
            var pumpState = GetSubscriptionPumpState(subscriberActivation);
            var subscriptionState =
                $"Messages={subscriberActivation.SubscriptionMessageCount},Dispatches={subscriberActivation.SubscriptionDispatchCount},Ignores={subscriberActivation.SubscriptionIgnoreCount},LastTopic={subscriberActivation.LastSubscriptionTopic},LastPacket={subscriberActivation.LastSubscriptionMessageName}";
            var publisherPeers = string.Join(';',
                publisherSnapshot.Peers.Select(static entry =>
                    $"{entry.Source}:{entry.PeerEndpoint}:{entry.State}:{entry.ChannelName}"));
            var subscriberPeers = string.Join(';',
                subscriberSnapshot.Peers.Select(static entry =>
                    $"{entry.Source}:{entry.PeerEndpoint}:{entry.State}:{entry.ChannelName}"));
            throw new TimeoutException(
                $"SPOT outbound publish failed. PublisherStatus={publisherSnapshot.Status}, PublisherPeers={publisherPeers}, PublisherSubjects={string.Join(';', publisherSnapshot.Subjects.Select(static entry => $"{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}/{entry.ActivePeerCount}"))}, SubscriberStatus={subscriberSnapshot.Status}, SubscriberPeers={subscriberPeers}, SubscriberSubjects={string.Join(';', subscriberSnapshot.Subjects.Select(static entry => $"{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}/{entry.ActivePeerCount}"))}, DirectPublishResult={directPublishResult}, RawReceived={rawReceived}, PumpState={pumpState}, SubscriptionState={subscriptionState}, ExternalEvents={recorder.ExternalEvents.Count}",
                ex);
        }

        Assert.Contains("external", recorder.ExternalEvents);

        await publisherHost.StopAsync();
        await subscriberHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task OutboundOnly_SpotPublisherClient_Reaches_RawSubscriber_On_FrameworkNode()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(registryRouterEndpoint));
                mesh.AddNode("subscriber-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.BindPubSub(subscriberNodeEndpoint);
                });
            });
            });
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(registryRouterEndpoint));
                mesh.AddNode("publisher-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.BindPubSub(publisherNodeEndpoint);
                });
                spot.AttachSpotPublisherClient("game.stage");
            });
            });
        });

        using var registryHost = registryBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await registryHost.StartAsync();
        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var publisherRuntime = publisherHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var subscriberRuntime = subscriberHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var publisher = publisherHost.Services.GetRequiredService<IZLinkSpotPublisherClient>();
        var subscriberNodeRuntime = GetSpotNodeRuntime(subscriberRuntime, "subscriber-node");

        await using var rawSubscriber = subscriberNodeRuntime.Node.CreateSpot();
        rawSubscriber.SetSubscription("stage.external");

        using var received = new global::Systems.Zlink.TopicMessage();
        await RetryAsync(
            async () =>
            {
                await publisher.PublishSpot(
                        "game.stage",
                        "stage.external",
                        new ExternalStageEvent("raw"))
                    .SubmitAsync();
                await Task.Yield();

                try
                {
                    return rawSubscriber.Subscribe(received,
                        global::Systems.Zlink.RecvFlags.DontWait);
                }
                catch (global::Systems.Zlink.ZlinkRecvException ex)
                    when (ex.Result == global::Systems.Zlink.ZlinkRecvException.ErrorCode.NoData)
                {
                    return false;
                }
            },
            static ok => ok,
            TimeSpan.FromSeconds(10));

        Assert.Equal("stage.external", received.Topic);

        await publisherHost.StopAsync();
        await subscriberHost.StopAsync();
        await registryHost.StopAsync();
    }

}

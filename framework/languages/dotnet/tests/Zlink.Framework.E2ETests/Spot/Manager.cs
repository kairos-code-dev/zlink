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


public sealed class ManagerTests : SpotTestSupport
{
    [Fact]
    public async Task SpotManager_Create_List_Remove_And_Publish_Work_Through_FrameworkRuntime()
    {
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();

        var host = await CreateHostAsync(ordersServer, spotNode);
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var events = host.Services.GetRequiredService<SpotEventsRecorder>();
            var orders = host.Services.GetRequiredService<OrdersRecorder>();

            var first = await manager.CreateAsync<StageSpot>();

            await RetryAsync(
                () => events.Initialized.Count >= 1
                    && orders.ReceivedScopes.Count >= 1,
                TimeSpan.FromSeconds(5));

            Assert.True(first.Created);

            var firstInfo = await manager.FindAsync(first.SpotRid);
            Assert.Equal(first.SpotRid, firstInfo?.SpotRid);

            var listed = await manager.ListAsync();
            Assert.Single(listed);

            Assert.Contains(events.ScopeId(first.SpotRid), orders.ReceivedScopes);

            Assert.True(await manager.RemoveAsync(first.SpotRid));
            Assert.Contains(first.SpotRid, events.Closing);
            Assert.Null(await manager.FindAsync(first.SpotRid));
            Assert.Empty(await manager.ListAsync());

            var firstScope = events.ScopeId(first.SpotRid);
            var second = await manager.CreateAsync<StageSpot>();
            await RetryAsync(
                () => events.Initialized.Count >= 2
                    && orders.ReceivedScopes.Count >= 2,
                TimeSpan.FromSeconds(5));
            Assert.True(second.Created);
            Assert.NotEqual(first.SpotRid, second.SpotRid);
            Assert.NotEqual(firstScope, events.ScopeId(second.SpotRid));
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_CreateAsync_Passes_Empty_CreatePayload_To_OnCreate()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var recorder = host.Services.GetRequiredService<SpotCreatePayloadRecorder>();

            var created = await manager.CreateAsync<CreatePayloadStageSpot>();

            Assert.True(created.Created);
            var payload = Assert.Single(recorder.Payloads);
            Assert.Empty(payload);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_GetOrCreateAsync_Initializes_Once_With_First_CreatePayload()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var recorder = host.Services.GetRequiredService<SpotCreatePayloadRecorder>();
            recorder.BlockCreate();
            var spotRid = RoutingId.From(Encoding.UTF8.GetBytes("payload-room"));
            using var firstA = Message.From("first-a");
            using var firstB = Message.From("first-b");
            using var second = Message.From("second");

            var first = manager.GetOrCreateAsync<CreatePayloadStageSpot>(
                spotRid,
                [firstA, firstB]).AsTask();
            await recorder.WaitCreateEnteredAsync();
            var secondResult = manager.GetOrCreateAsync<CreatePayloadStageSpot>(
                spotRid,
                [second]).AsTask();
            recorder.ReleaseCreate();

            var results = await Task.WhenAll(first, secondResult);

            Assert.Single(results, static result => result.Created);
            Assert.Single(results, static result => !result.Created);
            Assert.All(results, result => Assert.Equal(spotRid, result.SpotRid));
            var payload = Assert.Single(recorder.Payloads);
            Assert.Equal(["first-a", "first-b"], payload);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_GetOrCreateAsync_Returns_Existing_Spot_For_Same_Type()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var spotRid = RoutingId.From(Encoding.UTF8.GetBytes("payload-room-2"));

            var first = await manager.GetOrCreateAsync<CreatePayloadStageSpot>(spotRid);
            var second = await manager.GetOrCreateAsync<CreatePayloadStageSpot>(spotRid);

            Assert.True(first.Created);
            Assert.False(second.Created);
            Assert.Equal(first.SpotRid, second.SpotRid);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task Spot_Publish_Timer_And_Remove_Stop_Callbacks_Work()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.stage.timer.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        publisherBuilder.Services.AddScoped<SpotHeartbeatTimerHandler>();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
                mesh.AddNode("publisher-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.SetPubBind(publisherNodeEndpoint);
                });
                spot.AddSpotFactory<PublishingStageSpot>();
            });
            });
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        subscriberBuilder.Services.AddScoped<LocalStageEventHandler>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
                mesh.AddNode("subscriber-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.SetPubBind(subscriberNodeEndpoint);
                    pubsub.UseManualConnections(connections =>
                        connections.Connect(publisherNodeEndpoint));
                });
                spot.AddSpotFactory<LocalSubscriberStageSpot>();
            });
            });
        });

        using var registryHost = registryBuilder.Build();
        using var publisherHost = publisherBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();

        await registryHost.StartAsync();
        await publisherHost.StartAsync();
        await subscriberHost.StartAsync();

        var publisherManager = publisherHost.Services.GetRequiredService<IZLinkSpotManager>();
        var subscriberManager = subscriberHost.Services.GetRequiredService<IZLinkSpotManager>();
        var publisherRecorder = publisherHost.Services.GetRequiredService<SpotLifecycleRecorder>();

        _ = await subscriberManager.CreateAsync<LocalSubscriberStageSpot>();
        var created = await publisherManager.CreateAsync<PublishingStageSpot>();

        await RetryAsync(
            () => publisherRecorder.TickCount >= 2,
            TimeSpan.FromSeconds(10));

        var ticksBeforeRemove = publisherRecorder.TickCount;

        Assert.True(await publisherManager.RemoveAsync(created.SpotRid));
        await Task.Delay(300);
        Assert.Equal(ticksBeforeRemove, publisherRecorder.TickCount);

        await subscriberHost.StopAsync();
        await publisherHost.StopAsync();
        await registryHost.StopAsync();
    }

}

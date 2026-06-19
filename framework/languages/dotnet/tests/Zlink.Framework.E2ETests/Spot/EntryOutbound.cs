using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Spot;

public sealed class EntryOutboundTests : SpotTestSupport
{
    [Fact]
    public async Task EntrySpot_RequestToChannel_Uses_ManualAttachedChannelClient_AcrossHosts()
    {
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();

        var ordersBuilder = Host.CreateApplicationBuilder();
        ordersBuilder.Services.AddScoped<EntrySpotOrdersRequestHandler>();
        ordersBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<EntryOutboundTests>();
            {
                var channel = options.AddClientServerChannel("orders.api");
                channel.EnableServer(ordersServer);
                channel.AddRequestHandler<EntrySpotOrdersRequestHandler, EntrySpotOrderRequest, EntrySpotOrderReply>();

            }
        });

        var spotBuilder = Host.CreateApplicationBuilder();
        spotBuilder.Services.AddSingleton<EntrySpotCallbackRecorder>();
        spotBuilder.Services.AddScoped<GeneralEntrySpot>();
        spotBuilder.Services.AddScoped<EntrySpotChannelRequestHandler>();
        spotBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var mesh = options.AddSpotMesh("entry.manual.test");
                {
                    var spot = mesh.AddNode("entry-node");
                    {
                        var router = spot.EnableRouter(spotNode);

                    }
                    spot.AttachChannelClient("orders.api", ordersServer);
                    spot.AddEntrySpot<GeneralEntrySpot>();

                }

            }
        });

        var ordersHost = ordersBuilder.Build();
        var spotHost = spotBuilder.Build();
        await ordersHost.StartAsync();
        await spotHost.StartAsync();
        try
        {
            var runtime = spotHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = spotHost.Services.GetRequiredService<EntrySpotCallbackRecorder>();

            await RetryAsync(
                async () =>
                {
                    await InvokeEntrySpotPacketAsync(
                        runtime,
                        "entry-node",
                        "game.entry-general",
                        new EntrySpotChannelRequestCommand("manual"));
                    return recorder.Events.Contains("channel-reply:order:manual");
                },
                static result => result,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await StopAndDisposeHostAsync(spotHost);
            await StopAndDisposeHostAsync(ordersHost);
        }
    }

    [Fact]
    public async Task EntrySpot_RequestToChannel_Uses_DiscoveredAttachedChannelClient()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();
        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotCallbackRecorder>();
        builder.Services.AddScoped<GeneralEntrySpot>();
        builder.Services.AddScoped<EntrySpotChannelRequestHandler>();
        builder.Services.AddScoped<EntrySpotOrdersRequestHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);

            {
                var channel = options.AddClientServerChannel("orders.api");
                channel.EnableServer(ordersServer);
                channel.AddRequestHandler<EntrySpotOrdersRequestHandler, EntrySpotOrderRequest, EntrySpotOrderReply>();

            }
            {
                var mesh = options.AddSpotMesh("entry.test");
                mesh.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
                {
                    var spot = mesh.AddNode("entry-node");
                {
                    var router = spot.EnableRouter(spotNode);

                }
                spot.AttachChannelClient("orders.api");
                spot.AddEntrySpot<GeneralEntrySpot>();

                }

            }
        });

        var registryHost = registryBuilder.Build();
        var host = builder.Build();
        await registryHost.StartAsync();
        await host.StartAsync();
        try
        {
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = host.Services.GetRequiredService<EntrySpotCallbackRecorder>();

            await RetryAsync(
                async () =>
                {
                    await InvokeEntrySpotPacketAsync(
                        runtime,
                        "entry-node",
                        "game.entry-general",
                        new EntrySpotChannelRequestCommand("alpha"));
                    return recorder.Events.Contains("channel-reply:order:alpha");
                },
                static result => result,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
            await StopAndDisposeHostAsync(registryHost);
        }
    }

    [Fact]
    public async Task EntrySpot_Request_Continuation_Holds_The_EntrySpot_Line()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();
        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotCallbackRecorder>();
        builder.Services.AddScoped<GeneralEntrySpot>();
        builder.Services.AddScoped<EntrySpotChannelRequestHandler>();
        builder.Services.AddScoped<EntrySpotGeneralRecordingHandler>();
        builder.Services.AddScoped<EntrySpotGeneralBlockingHandler>();
        builder.Services.AddScoped<EntrySpotGatedOrdersRequestHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);

            {
                var channel = options.AddClientServerChannel("orders.api");
                channel.EnableServer(ordersServer);
                channel.AddRequestHandler<EntrySpotGatedOrdersRequestHandler, EntrySpotOrderRequest, EntrySpotOrderReply>();

            }
            {
                var mesh = options.AddSpotMesh("entry.test");
                mesh.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
                {
                    var spot = mesh.AddNode("entry-node");
                {
                    var router = spot.EnableRouter(spotNode);

                }
                spot.AttachChannelClient("orders.api");
                spot.AddEntrySpot<GeneralEntrySpot>();

                }

            }
        });

        var registryHost = registryBuilder.Build();
        var host = builder.Build();
        await registryHost.StartAsync();
        await host.StartAsync();
        try
        {
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = host.Services.GetRequiredService<EntrySpotCallbackRecorder>();

            // The requesting handler awaits the channel reply; the orders
            // server holds that reply until released.
            var requestDispatch = InvokeEntrySpotPacketAsync(
                runtime,
                "entry-node",
                "game.entry-general",
                new EntrySpotChannelRequestCommand("gated"));
            await recorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(10));

            var recordDispatch = InvokeEntrySpotPacketAsync(
                runtime,
                "entry-node",
                "game.entry-general",
                new EntrySpotGeneralRecordCommand("during-request"));

            // Non-reentrant gate: the awaiting handler holds the Entry Spot
            // line, so the record packet must not run before the reply lands.
            await Task.Delay(TimeSpan.FromMilliseconds(300));
            Assert.DoesNotContain(
                recorder.Events,
                e => e.StartsWith("record:during-request:", StringComparison.Ordinal));
            Assert.False(recordDispatch.IsCompleted, "packet ran while a handler was awaiting a request.");

            recorder.ReleaseBlocking.TrySetResult();
            await Task.WhenAll(requestDispatch, recordDispatch).WaitAsync(TimeSpan.FromSeconds(10));

            var events = recorder.Events.ToArray();
            var replyIndex = Array.IndexOf(events, "channel-reply:order:gated");
            var recordIndex = Array.FindIndex(
                events,
                e => e.StartsWith("record:during-request:", StringComparison.Ordinal));
            Assert.True(replyIndex >= 0, "channel reply continuation event missing.");
            Assert.True(recordIndex >= 0, "record handler event missing.");
            Assert.True(
                replyIndex < recordIndex,
                "queued packet ran before the awaited request continuation completed.");
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
            await StopAndDisposeHostAsync(registryHost);
        }
    }
}

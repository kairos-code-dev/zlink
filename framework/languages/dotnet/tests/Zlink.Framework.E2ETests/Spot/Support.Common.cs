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

public abstract partial class SpotTestSupport
{
    private protected static readonly TimeSpan PollingInterval = TimeSpan.FromMilliseconds(150);

    private protected static async Task<IHost> CreateHostAsync(
        string ordersServer,
        string spotNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotEventsRecorder>();
        builder.Services.AddSingleton<OrdersRecorder>();
        builder.Services.AddScoped<SpotScopeMarker>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<SpotTestSupport>();


            options.AddClientServerChannel("orders", channel =>
            {
                channel.EnableServer(server => server.Bind(ordersServer));
                channel.AddHandlerGroup("stage-orders");
            });

            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("stage-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(spotNode);
                });
                spot.AttachChannelClient("orders", client =>
                {
                    client.UseManualConnections(connections => connections.Connect(ordersServer));
                });
                spot.AddSpotFactory<StageSpot>();
            });
            });
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private protected static async Task<IHost> CreatePayloadHostAsync(string spotNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotCreatePayloadRecorder>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("payload-node", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("payload-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(spotNode);
                });
                spot.AddSpotFactory<CreatePayloadStageSpot>();
            });
            });
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private protected static async Task<IHost> CreateSpotRouteTransportHostAsync(
        SpotRouteTransportKind transportKind,
        string routerChannelId,
        string channelEndpoint,
        string spotNodeEndpoint)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotRouteTransportRecorder>();
        builder.Services.AddSingleton<FixedSpotRemoteAddressResolver>();
        builder.Services.AddSingleton<IZLinkSpotRemoteAddressResolver>(
            services => services.GetRequiredService<FixedSpotRemoteAddressResolver>());
        builder.Services.AddScoped<SpotRouteTargetCommandHandler>();
        builder.Services.AddScoped<SpotRouteTargetRequestHandler>();
        builder.Services.AddScoped<SpotRouteSendCallerHandler>();
        builder.Services.AddScoped<SpotRouteRequestCallerHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            AddRouterChannel(options, transportKind, routerChannelId, channelEndpoint);

            options.AddSpotMesh("spot.route.transport", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("route-target-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(GetFreeTcpEndpoint());
                    router.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.From(
                            Encoding.UTF8.GetBytes("target-node"));
                    });
                });
                spot.AcceptSpotRoutesFromChannel(
                    routerChannelId,
                    routes => routes.UseManualConnections(
                        peers => peers.Connect(channelEndpoint)));
                spot.AddEntrySpot<SpotRouteCallerEntrySpot>();
                spot.AddSpotFactory<SpotRouteTargetSpot>();
            });
            });
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private protected static void AddRouterChannel(
        IZLinkFrameworkOptions options,
        SpotRouteTransportKind transportKind,
        string routerChannelId,
        string channelEndpoint)
    {
        switch (transportKind)
        {
            case SpotRouteTransportKind.ClientServer:
                options.AddClientServerChannel(routerChannelId, channel =>
                {
                    channel.EnableServer(server =>
                    {
                        server.Bind(channelEndpoint);
                        server.ConfigureRouting(routing =>
                        {
                            routing.RoutingId = RoutingId.From("aabbcc10");
                        });
                    });
                });
                break;

            case SpotRouteTransportKind.RouteMesh:
                options.AddRouteMeshChannel(routerChannelId, route =>
                {
                    route.Bind(channelEndpoint);
                    route.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.From("aabbcc11");
                    });
                });
                break;

            default:
                throw new ArgumentOutOfRangeException(nameof(transportKind), transportKind, null);
        }
    }

    private protected static async Task InvokeEntrySpotPacketAsync<TMessage>(
        ZLinkFrameworkRuntime runtime,
        string spotNodeName,
        string channelName,
        TMessage message)
    {
        var activation = runtime.GetSpotNodeRuntime(spotNodeName).EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation was not created.");
        var header = CreateEntrySpotEnvelopeHeader(channelName, message);
        Assert.True(activation.TryResolvePacket(header, out var descriptor));
        await activation.InvokePacketAsync(descriptor!, message, CancellationToken.None)
            .ConfigureAwait(false);
    }

    private protected static async Task VerifyAcceptedRouteChannelSendToSpotAsync(
        SpotRouteTransportKind transportKind,
        string routerChannelId)
    {
        var channelEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var host = await CreateSpotRouteTransportHostAsync(
            transportKind,
            routerChannelId,
            channelEndpoint,
            spotNodeEndpoint);
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = host.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            var target = await manager.GetOrCreateAsync<SpotRouteTargetSpot>(
                RoutingId.From(Encoding.UTF8.GetBytes($"spot{routerChannelId}")));
            await WaitForAcceptedRoutePeerAsync(nodeRuntime, routerChannelId);

            await RetryAsync(
                async () =>
                {
                    var header = ZLinkClientCallCodec.CreateEnvelope(
                        ZLinkMessageKind.Command,
                        routerChannelId,
                        ZLinkMessageNameResolver.ResolveFromType(typeof(SpotRouteTargetCommand))
                            ?? throw new InvalidOperationException("Message name is required."));
                    var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                        header,
                        new SpotRouteTargetCommand($"direct:{routerChannelId}"));
                    await runtime.SendSpotViaRouterChannelAsync(
                            routerChannelId,
                            nodeRuntime.Node.RoutingId,
                            target.SpotRid,
                            parts,
                            CancellationToken.None)
                        .ConfigureAwait(false);
                    return recorder.Commands.Contains($"direct:{routerChannelId}");
                },
                static result => result,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    private protected static async Task RetryAsync(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
            {
                return;
            }

            await Task.Delay(PollingInterval);
        }

        throw new TimeoutException("SPOT integration retry timed out.");
    }

    private protected static async Task<T> RetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        Exception? lastError = null;

        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(PollingInterval);
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("SPOT integration retry timed out.");
    }

    private protected static string GetFreeTcpEndpoint()
    {
        return ChannelMessagingTestSupport.GetTcpEndpoint();
    }

    private protected static async Task WaitForAcceptedRoutePeerAsync(
        ZLinkSpotNodeRuntime nodeRuntime,
        string channelName,
        bool requireConnected = false)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        IReadOnlyList<ZLinkSpotNodePeerEntry> peers = [];
        while (DateTime.UtcNow < deadline)
        {
            peers = nodeRuntime.Node.PeersSnapshot();
            if (peers.Any(peer => peer.ChannelName == channelName
                    && (!requireConnected || peer.State == ZLinkSpotPeerState.Connected)))
            {
                await Task.Delay(TimeSpan.FromMilliseconds(300));
                return;
            }

            await Task.Delay(PollingInterval);
        }

        var peerSummary = string.Join(
            ", ",
            peers.Select(peer => $"{peer.ChannelName}:{peer.PeerEndpoint}:{peer.State}"));
        throw new TimeoutException(
            $"SPOT route peer '{channelName}' did not connect. Peers: {peerSummary}");
    }

    private protected static async Task StopAndDisposeHostAsync(IHost host)
    {
        try
        {
            await host.StopAsync();
        }
        finally
        {
            host.Dispose();
        }
    }
}

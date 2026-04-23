using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class SpotIntegrationTests
{
    [Fact]
    public async Task SpotManager_Create_List_Remove_And_Publish_Work_Through_FrameworkRuntime()
    {
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();

        using var host = await CreateHostAsync(ordersServer, spotNode);
        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var events = host.Services.GetRequiredService<SpotEventsRecorder>();
        var orders = host.Services.GetRequiredService<OrdersRecorder>();

        var first = await manager.CreateAsync("stage");

        await RetryAsync(
            () => events.Initialized.Count >= 1
                && orders.ReceivedScopes.Count >= 1,
            TimeSpan.FromSeconds(5));

        Assert.True(first.Created);

        var firstInfo = await manager.GetAsync(first.SpotRid);
        Assert.Equal("stage", firstInfo?.SpotName);

        var listed = await manager.ListAsync();
        Assert.Single(listed);

        Assert.Contains(events.ScopeId(first.SpotRid), orders.ReceivedScopes);

        Assert.True(await manager.RemoveAsync(first.SpotRid));
        Assert.Null(await manager.GetAsync(first.SpotRid));
        Assert.Empty(await manager.ListAsync());

        var firstScope = events.ScopeId(first.SpotRid);
        var second = await manager.CreateAsync("stage");
        await RetryAsync(
            () => events.Initialized.Count >= 2
                && orders.ReceivedScopes.Count >= 2,
            TimeSpan.FromSeconds(5));
        Assert.True(second.Created);
        Assert.NotEqual(first.SpotRid, second.SpotRid);
        Assert.NotEqual(firstScope, events.ScopeId(second.SpotRid));
    }

    private static async Task<IHost> CreateHostAsync(
        string ordersServer,
        string spotNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotEventsRecorder>();
        builder.Services.AddSingleton<OrdersRecorder>();
        builder.Services.AddScoped<SpotScopeMarker>();
        builder.Services.AddZLinkHandlersFromAssemblyContaining<SpotIntegrationTests>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", _ => { });

            options.AddChannel("orders", channel =>
            {
                channel.EnableServer(server => server.Bind(ordersServer));
            });

            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AttachChannelClient("orders", client =>
                {
                    client.UseManualConnections(connections => connections.Connect(ordersServer));
                });
                spot.AddSpotFactory<StageSpot>("stage");
            });
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private static async Task RetryAsync(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
            {
                return;
            }

            await Task.Delay(50);
        }

        throw new TimeoutException("SPOT integration retry timed out.");
    }

    private static string GetFreeTcpEndpoint()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        return $"tcp://127.0.0.1:{endpoint.Port}";
    }

    public sealed class StageSpot : ZLinkSpot
    {
        private readonly SpotScopeMarker _scopeMarker;
        private readonly SpotEventsRecorder _events;
        private readonly IZLinkSpotClient _spotClient;

        public StageSpot(
            global::Zlink.RoutingId spotRid,
            global::Zlink.RoutingId nodeRid,
            SpotScopeMarker scopeMarker,
            SpotEventsRecorder events,
            IZLinkSpotClient spotClient)
            : base(spotRid, nodeRid)
        {
            _scopeMarker = scopeMarker;
            _events = events;
            _spotClient = spotClient;
        }

        public string ScopeId => _scopeMarker.Id;

        public override async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _events.RecordInitialized(SpotRid, _scopeMarker.Id);

            Assert.True(_spotClient.SendChannel("orders", new StageBootCommand(_scopeMarker.Id)).Exec());
            await ValueTask.CompletedTask;
        }
    }

    public sealed record StageBootCommand(string ScopeId);

    public sealed class StageOrdersHandler(OrdersRecorder recorder)
    {
        [ZLinkSend]
        public ValueTask HandleAsync(
            StageBootCommand request,
            ZLinkSendContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            recorder.ReceivedScopes.Add(request.ScopeId);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class SpotScopeMarker
    {
        public string Id { get; } = Guid.NewGuid().ToString("N");
    }

    public sealed class OrdersRecorder
    {
        public ConcurrentBag<string> ReceivedScopes { get; } = [];
    }

    public sealed class SpotEventsRecorder
    {
        private readonly ConcurrentDictionary<global::Zlink.RoutingId, string> _scopes = [];
        public ConcurrentDictionary<global::Zlink.RoutingId, string> Initialized => _scopes;

        public void RecordInitialized(global::Zlink.RoutingId spotRid, string scopeId)
        {
            _scopes[spotRid] = scopeId;
        }
        public string? ScopeId(global::Zlink.RoutingId spotRid)
        {
            return _scopes.TryGetValue(spotRid, out var scopeId) ? scopeId : null;
        }
    }
}

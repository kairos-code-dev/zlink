using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

public sealed class RouteUpdatesTests : StreamTestSupport
{
    [Fact]
    public async Task SessionActorRouteUpdate_Changes_Attached_TargetNodeRid()
    {
        var endpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("1221");
        var firstRid = RoutingId.FromString("1222");
        var secondRid = RoutingId.FromString("1223");
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(endpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = localRid);
                    routed.UseManualConnections(connections => connections.Connect(endpoint));
                });
            });
        });

        try
        {
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var context = new ZLinkSessionContext(
                runtime,
                host.Services.GetRequiredService<IZLinkClient>(),
                new SpotTestSupport.TestStream("route-update-session"),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var actor = (ZLinkActorRef)await context.BindActorHandleAsync(
                "remote-player",
                "player",
                new ZLinkActorRoute("gateway", firstRid, 1));

            var updated = await runtime.UpdateAttachedActorRouteAsync(
                "remote-player",
                "gateway",
                secondRid,
                expectedActorGeneration: 1,
                newActorGeneration: 2,
                CancellationToken.None);

            Assert.Equal(1, updated);
            Assert.Equal(secondRid, actor.TargetNodeRid);
            Assert.Equal(2UL, actor.ActorGeneration);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task SessionActorRouteUpdate_Ignores_Stale_ExpectedActorGeneration()
    {
        var endpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("1231");
        var firstRid = RoutingId.FromString("1232");
        var staleRid = RoutingId.FromString("1233");
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(endpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = localRid);
                    routed.UseManualConnections(connections => connections.Connect(endpoint));
                });
            });
        });

        try
        {
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var context = new ZLinkSessionContext(
                runtime,
                host.Services.GetRequiredService<IZLinkClient>(),
                new SpotTestSupport.TestStream("stale-route-update-session"),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var actor = (ZLinkActorRef)await context.BindActorHandleAsync(
                "remote-player",
                "player",
                new ZLinkActorRoute("gateway", firstRid, 5));

            var updated = await runtime.UpdateAttachedActorRouteAsync(
                "remote-player",
                "gateway",
                staleRid,
                expectedActorGeneration: 4,
                newActorGeneration: 6,
                CancellationToken.None);

            Assert.Equal(0, updated);
            Assert.Equal(firstRid, actor.TargetNodeRid);
            Assert.Equal(5UL, actor.ActorGeneration);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task SessionActorRouteUpdate_Allows_Idempotent_Same_Target()
    {
        var endpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("1241");
        var firstRid = RoutingId.FromString("1242");
        var secondRid = RoutingId.FromString("1243");
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(endpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = localRid);
                    routed.UseManualConnections(connections => connections.Connect(endpoint));
                });
            });
        });

        try
        {
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var context = new ZLinkSessionContext(
                runtime,
                host.Services.GetRequiredService<IZLinkClient>(),
                new SpotTestSupport.TestStream("idempotent-route-update-session"),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var actor = (ZLinkActorRef)await context.BindActorHandleAsync(
                "remote-player",
                "player",
                new ZLinkActorRoute("gateway", firstRid, 1));

            var firstUpdate = await runtime.UpdateAttachedActorRouteAsync(
                "remote-player",
                "gateway",
                secondRid,
                expectedActorGeneration: 1,
                newActorGeneration: 2,
                CancellationToken.None);
            var secondUpdate = await runtime.UpdateAttachedActorRouteAsync(
                "remote-player",
                "gateway",
                secondRid,
                expectedActorGeneration: 1,
                newActorGeneration: 2,
                CancellationToken.None);

            Assert.Equal(1, firstUpdate);
            Assert.Equal(1, secondUpdate);
            Assert.Equal(secondRid, actor.TargetNodeRid);
            Assert.Equal(2UL, actor.ActorGeneration);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task SessionActorRouteUpdate_Rejects_Conflicting_Target_For_Same_ExpectedGeneration()
    {
        var endpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("1251");
        var firstRid = RoutingId.FromString("1252");
        var secondRid = RoutingId.FromString("1253");
        var conflictingRid = RoutingId.FromString("1254");
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(endpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = localRid);
                    routed.UseManualConnections(connections => connections.Connect(endpoint));
                });
            });
        });

        try
        {
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var context = new ZLinkSessionContext(
                runtime,
                host.Services.GetRequiredService<IZLinkClient>(),
                new SpotTestSupport.TestStream("conflict-route-update-session"),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var actor = (ZLinkActorRef)await context.BindActorHandleAsync(
                "remote-player",
                "player",
                new ZLinkActorRoute("gateway", firstRid, 1));

            var applied = await runtime.UpdateAttachedActorRouteAsync(
                "remote-player",
                "gateway",
                secondRid,
                expectedActorGeneration: 1,
                newActorGeneration: 2,
                CancellationToken.None);
            var rejected = await runtime.UpdateAttachedActorRouteAsync(
                "remote-player",
                "gateway",
                conflictingRid,
                expectedActorGeneration: 1,
                newActorGeneration: 3,
                CancellationToken.None);

            Assert.Equal(1, applied);
            Assert.Equal(0, rejected);
            Assert.Equal(secondRid, actor.TargetNodeRid);
            Assert.Equal(2UL, actor.ActorGeneration);
        }
        finally
        {
            await host.StopAsync();
        }
    }

}

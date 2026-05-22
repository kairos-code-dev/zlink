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

public sealed class ActorBindingTests : StreamTestSupport
{
    [Fact]
    public async Task ActorManager_CreateAsync_Throws_When_ActorAlreadyExists()
    {
        var spotEndpoint = GetFreeTcpEndpoint();
        var host = await CreateHostAsync("actor-manager", services =>
        {
            services.AddSingleton<ActorDispatchRecorder>();
            services.AddScoped<GatewayActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotNode("actor-node", spot =>
                {
                    spot.Bind(spotEndpoint);
                });
            });
        });

        try
        {
            var actors = host.Services.GetRequiredService<IZLinkActorManager>();
            var recorder = host.Services.GetRequiredService<ActorDispatchRecorder>();

            var created = await actors.CreateAsync("actor-duplicate", "player");
            var found = await actors.FindAsync("actor-duplicate");
            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => actors.CreateAsync("actor-duplicate", "player").AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorAlreadyExists, ex.Kind);
            Assert.Same(created, found);
            Assert.Equal(1, recorder.CreatedCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task ActorManager_GetOrCreateAsync_Reuses_ExistingActor_And_Rejects_TypeMismatch()
    {
        var spotEndpoint = GetFreeTcpEndpoint();
        var host = await CreateHostAsync("actor-manager", services =>
        {
            services.AddSingleton<ActorDispatchRecorder>();
            services.AddScoped<GatewayActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddActorFactory<GatewayActorFactory>("spectator");
                options.AddSpotNode("actor-node", spot =>
                {
                    spot.Bind(spotEndpoint);
                });
            });
        });

        try
        {
            var actors = host.Services.GetRequiredService<IZLinkActorManager>();
            var recorder = host.Services.GetRequiredService<ActorDispatchRecorder>();

            var created = await actors.GetOrCreateAsync("actor-reuse", "player");
            var reused = await actors.GetOrCreateAsync("actor-reuse", "player");
            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => actors.GetOrCreateAsync("actor-reuse", "spectator").AsTask());

            Assert.Same(created, reused);
            Assert.Equal(ZLinkFrameworkErrorKind.ActorTypeMismatch, ex.Kind);
            Assert.Equal(1, recorder.CreatedCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task ActorManager_CreateAsync_Clears_State_When_Configure_Fails()
    {
        var spotEndpoint = GetFreeTcpEndpoint();
        var recorder = new ConfigureFailureRecorder();
        var host = await CreateHostAsync("actor-manager", services =>
        {
            services.AddSingleton(recorder);
            services.AddScoped<ConfigureFailureActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<ConfigureFailureActorFactory>("player");
                options.AddSpotNode("actor-node", spot =>
                {
                    spot.Bind(spotEndpoint);
                });
            });
        });

        try
        {
            var actors = host.Services.GetRequiredService<IZLinkActorManager>();

            var ex = await Assert.ThrowsAsync<InvalidOperationException>(
                () => actors.CreateAsync("actor-configure-retry", "player").AsTask());
            recorder.FailConfigure = false;
            var foundAfterFailure = await actors.FindAsync("actor-configure-retry");
            var createdAfterRetry = await actors.CreateAsync("actor-configure-retry", "player");

            Assert.Equal("configure failed", ex.Message);
            Assert.Null(foundAfterFailure);
            Assert.Equal("actor-configure-retry", createdAfterRetry.ActorId);
            Assert.Equal(2, recorder.CreatedCount);
            Assert.Equal(2, recorder.ConfigureCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task BindActorHandleAsync_DoesNot_Create_LocalActor()
    {
        var endpoint = GetFreeTcpEndpoint();
        var spotEndpoint = GetFreeTcpEndpoint();
        var recorder = new ActorDispatchRecorder();
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotNode("actor-node", spot =>
                {
                    spot.Bind(spotEndpoint);
                });
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(endpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = RoutingId.FromString("1001"));
                    routed.UseManualConnections(connections => connections.Connect(endpoint));
                });
            });
        });

        try
        {
            var context = new ZLinkSessionContext(
                host.Services.GetRequiredService<ZLinkFrameworkRuntime>(),
                new SpotTestSupport.TestStream("missing-actor-session"),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => context.BindActorHandleAsync("missing-player", "player").AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, ex.Kind);
            Assert.Equal(0, recorder.CreatedCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task SessionActorBind_Does_Not_Resolve_RemoteAddress()
    {
        var endpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("1201");
        var remoteRid = RoutingId.FromString("1202");
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton<IZLinkActorRemoteAddressResolver, ThrowingActorRemoteAddressResolver>();
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
            var context = new ZLinkSessionContext(
                host.Services.GetRequiredService<ZLinkFrameworkRuntime>(),
                new SpotTestSupport.TestStream("explicit-route-session"),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var actor = await context.BindActorHandleAsync(
                "remote-player",
                "player",
                new ZLinkActorRemoteAddress("gateway", remoteRid, 1));

            Assert.Equal("remote-player", actor.ActorId);
            Assert.True(host.Services.GetRequiredService<ZLinkFrameworkRuntime>()
                .TryGetActorBoundSession("remote-player", out _));
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task SessionActorBind_Rejects_Unchecked_ActorGeneration()
    {
        var endpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("1211");
        var remoteRid = RoutingId.FromString("1212");
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
            var context = new ZLinkSessionContext(
                host.Services.GetRequiredService<ZLinkFrameworkRuntime>(),
                new SpotTestSupport.TestStream("unchecked-route-session"),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => context.BindActorHandleAsync(
                        "remote-player",
                        "player",
                        new ZLinkActorRemoteAddress("gateway", remoteRid, 0))
                    .AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, ex.Kind);
            Assert.False(host.Services.GetRequiredService<ZLinkFrameworkRuntime>()
                .TryGetActorBoundSession("remote-player", out _));
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task SessionActorBind_WithoutRoute_Is_LocalOnly()
    {
        var endpoint = GetFreeTcpEndpoint();
        var spotEndpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("1213");
        var recorder = new ActorDispatchRecorder();
        var routeResolver = new CountingActorRemoteAddressResolver(new ZLinkActorRemoteAddress(
            "gateway",
            RoutingId.FromString("1214"),
            1));
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddSingleton<IZLinkActorRemoteAddressResolver>(routeResolver);
            services.AddScoped<GatewayActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotNode("actor-node", spot =>
                {
                    spot.Bind(spotEndpoint);
                });
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
            var context = new ZLinkSessionContext(
                host.Services.GetRequiredService<ZLinkFrameworkRuntime>(),
                new SpotTestSupport.TestStream("local-only-session"),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => context.BindActorHandleAsync("missing-player", "player").AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, ex.Kind);
            Assert.Equal(0, recorder.CreatedCount);
            Assert.Equal(0, routeResolver.CallCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

}

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
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(spotEndpoint);
                    });
                });
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
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(spotEndpoint);
                    });
                });
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
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(spotEndpoint);
                    });
                });
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
    public async Task BindActorAsync_DoesNot_Create_LocalActor()
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
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(spotEndpoint);
                    });
                });
                });
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(endpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = RoutingId.From("1001"));
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
                () => context.Actors.BindAsync(
                    new MissingBindActor("missing-player")).AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, ex.Kind);
            Assert.Equal(0, recorder.CreatedCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task BindActorAsync_Can_Find_SessionBoundActor_ByActorId()
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
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                    {
                        spot.EnableRouter(router =>
                        {
                            router.BindRouter(spotEndpoint);
                        });
                    });
                });
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(endpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = RoutingId.From("1101"));
                    routed.UseManualConnections(connections => connections.Connect(endpoint));
                });
            });
        });

        try
        {
            var actors = host.Services.GetRequiredService<IZLinkActorManager>();
            var actor1 = await actors.CreateAsync("bound-player-1", "player");
            var actor2 = await actors.CreateAsync("bound-player-2", "player");

            var context = new ZLinkSessionContext(
                host.Services.GetRequiredService<ZLinkFrameworkRuntime>(),
                new RoutedTestStream("actor-lookup-session", RoutingId.From("session-lookup")),
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var first = await context.Actors.BindAsync(actor1);
            var second = await context.Actors.BindAsync(actor2);

            var foundFirst = context.Actors.Find("bound-player-1");
            var foundSecond = context.Actors.Find("bound-player-2");
            Assert.Null(context.Actors.Find("missing-player"));
            Assert.Same(first, foundFirst);
            Assert.Same(second, foundSecond);

            await context.CleanupActorBindingsAsync(CancellationToken.None);
            Assert.Null(context.Actors.Find("bound-player-1"));
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
        var localRid = RoutingId.From("1213");
        var recorder = new ActorDispatchRecorder();
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(spotEndpoint);
                    });
                });
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
                () => context.Actors.BindAsync(
                    new MissingBindActor("missing-player")).AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, ex.Kind);
            Assert.Equal(0, recorder.CreatedCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    private sealed class RoutedTestStream(
        string sessionId,
        RoutingId routingId) : IZLinkStream
    {
        public string SessionId { get; } = sessionId;

        public RoutingId? RoutingId { get; } = routingId;

        public string? LocalAddr => "local";

        public string? RemoteAddr => "remote";

        public bool Write(Message payload, SendFlags flags = SendFlags.None)
        {
            _ = payload;
            _ = flags;
            return true;
        }

        public ValueTask CloseAsync(CancellationToken cancellationToken = default)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class MissingBindActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context
            => throw new InvalidOperationException("Missing bind actor does not expose a runtime context.");
    }
}

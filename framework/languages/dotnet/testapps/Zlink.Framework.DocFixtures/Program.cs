using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;

_ = FixtureSamples.CreateChannelBuilder();
_ = FixtureSamples.CreateSpotBuilder();
_ = FixtureSamples.CreateStreamBuilder();
_ = FixtureSamples.CreateRegistryBuilder();
_ = FixtureSamples.CreateMonitoringBuilder();
_ = FixtureSamples.CreateActorBuilder();

return 0;

internal static class FixtureSamples
{
    public static IHostApplicationBuilder CreateChannelBuilder()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<FixtureSendHandler>();
            options.UseDiscovery(discovery =>
            {
                discovery.Add("tcp://127.0.0.1:7100");
            });

            options.AddClientServerChannel("orders", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind("tcp://127.0.0.1:7201");
                });

                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections =>
                    {
                        connections.Connect("tcp://127.0.0.1:7201");
                    });
                });

            });

            options.AddFanoutChannel("orders.events", channel =>
            {
                channel.EnablePublisher(publisher =>
                {
                    publisher.Bind("tcp://127.0.0.1:7202");
                });
                channel.EnableSubscriber(subscriber =>
                {
                    subscriber.UseManualConnections(connections =>
                    {
                        connections.Connect("tcp://127.0.0.1:7202");
                    });
                });
            });
        });
        return builder;
    }

    public static IHostApplicationBuilder CreateSpotBuilder()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddScoped<FixtureSpotTimerHandler>();
        builder.Services.AddScoped<FixtureSpotSubscriptionHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("game.stage", spotMesh =>
            {
                spotMesh.UseDiscovery(discovery =>
                {
                    discovery.Add("tcp://127.0.0.1:7300");
                });
                spotMesh.AddNode("stage-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind("tcp://127.0.0.1:7302");
                    });
                    spot.EnablePubSub(pubsub =>
                    {
                        pubsub.SetPubBind("tcp://127.0.0.1:7301");
                    });
                    spot.AttachChannelClient("orders", client =>
                    {
                        client.UseManualConnections(connections =>
                        {
                            connections.Connect("tcp://127.0.0.1:7201");
                        });
                    });
                    spot.AttachSpotPublisherClient("game.stage");
                    spot.AddSpotFactory<FixtureStageSpot>();
                });
            });
        });
        return builder;
    }

    public static IHostApplicationBuilder CreateStreamBuilder()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddScoped<FixtureRawStreamSession>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddStreamNode("stream.raw", stream =>
            {
                stream.Bind("tcp://127.0.0.1:7401");
                stream.RegisterSession<FixtureRawStreamSession>();
            });
        });
        return builder;
    }

    public static IHostApplicationBuilder CreateRegistryBuilder()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = "tcp://127.0.0.1:7501";
            options.RouterEndpoint = "tcp://127.0.0.1:7502";
        });
        return builder;
    }

    public static IHostApplicationBuilder CreateMonitoringBuilder()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = "tcp://127.0.0.1:7601";
            options.RouterEndpoint = "tcp://127.0.0.1:7602";
        });
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add("tcp://127.0.0.1:7602");
            });

            options.AddClientServerChannel("orders", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind("tcp://127.0.0.1:7603");
                });
            });

            options.AddSpotMesh("game.stage", spotMesh =>
            {
                spotMesh.UseDiscovery(discovery =>
                {
                    discovery.Add("tcp://127.0.0.1:7602");
                });
                spotMesh.AddNode("stage-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind("tcp://127.0.0.1:7605");
                    });
                    spot.EnablePubSub(pubsub =>
                    {
                        pubsub.SetPubBind("tcp://127.0.0.1:7604");
                    });
                    spot.AddSpotFactory<FixtureStageSpot>();
                });
            });
        });
        builder.Services.AddZLinkMonitoring(options =>
        {
            options.AddSocketEvents("orders.server", ZLinkSocketEventKind.ConnectionReady);
            options.AddRegistryEvents("registry", TimeSpan.FromMilliseconds(250));
            options.AddSpotEvents("stage-node", TimeSpan.FromMilliseconds(250));
        });
        return builder;
    }

    public static IHostApplicationBuilder CreateActorBuilder()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddScoped<FixtureActorJoinHandler>();
        builder.Services.AddScoped<FixtureActorPacketSession>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<FixtureActorFactory>("hero");

            options.AddStreamNode("stream.actor", stream =>
            {
                stream.Bind("tcp://127.0.0.1:7701");
                stream.RegisterSession<FixtureActorPacketSession>();
            });

            options.AddSpotMesh("game.stage", spotMesh =>
            {
                spotMesh.UseDiscovery(discovery =>
                {
                    discovery.Add("tcp://127.0.0.1:7700");
                });
                spotMesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind("tcp://127.0.0.1:7702");
                    });
                    spot.AddSpotFactory<FixtureActorSpot>();
                });
            });
        });
        return builder;
    }
}

internal sealed class FixtureStageSpot(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddSubscribe<FixtureSpotSubscriptionHandler>("stage.event");
    }

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        _ = await Context.AddTimer<FixtureSpotTimerHandler>(
            "heartbeat",
            TimeSpan.FromSeconds(1),
            cancellationToken: cancellationToken);
    }
}

internal sealed class FixtureSpotTimerHandler
    : IZLinkSpotTimerHandler<FixtureStageSpot>
{
    public async ValueTask HandleAsync(
        FixtureStageSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        _ = tick;
        await spot.Context.Outbound.Publish("stage.event", new FixtureSpotEvent(spot.Context.SpotRid.ToHex()))
            .Submit(cancellationToken);
    }
}

internal sealed class FixtureSpotSubscriptionHandler
    : IZLinkSpotSubscriptionHandler<FixtureStageSpot, FixtureSpotEvent>
{
    public ValueTask HandleAsync(
        FixtureStageSpot spot,
        FixtureSpotEvent message,
        CancellationToken cancellationToken)
    {
        _ = spot;
        _ = message;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed record FixtureSpotEvent(string Value);

internal sealed class FixtureSendHandler
{
    [ZLinkSend]
    public ValueTask HandleAsync(
        FixtureSendCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _ = command;
        _ = context;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed record FixtureSendCommand(string Value);

internal sealed class FixtureRawStreamSession(IZLinkSessionContext context) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = Context;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        global::Systems.Zlink.Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        _ = payload;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed class FixtureActorSpot(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddActorJoin<FixtureActorJoinHandler, FixtureActor, FixtureActorJoinRequest, FixtureActorJoinReply>();
    }
}

internal sealed class FixtureActorJoinHandler
    : IZLinkSpotActorJoinHandler<FixtureActorSpot, FixtureActor, FixtureActorJoinRequest, FixtureActorJoinReply>
{
    public async ValueTask<FixtureActorJoinReply> HandleAsync(
        FixtureActorSpot spot,
        FixtureActor actor,
        FixtureActorJoinRequest request,
        CancellationToken cancellationToken)
    {
        actor.AttachSpot(spot);
        await actor.OnAttachedAsync(cancellationToken);
        return new FixtureActorJoinReply(request.RoomId);
    }
}

internal sealed record FixtureActorJoinRequest(string RoomId);

internal sealed record FixtureActorJoinReply(string RoomId);

internal sealed class FixtureActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new FixtureActor(actorId, context));
    }
}

internal sealed class FixtureActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public FixtureActorSpot? Spot { get; private set; }

    public IZLinkActorContext Context { get; } = context;

    public void AttachSpot(FixtureActorSpot spot)
    {
        Spot = spot;
    }

    public void DetachSpot(FixtureActorSpot spot)
    {
        if (ReferenceEquals(Spot, spot))
        {
            Spot = null;
        }
    }

    public ValueTask OnAttachedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDetachedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        global::Systems.Zlink.Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        _ = payload;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed class FixtureActorPacketSession(
    IZLinkSessionContext context,
    IZLinkActorManager actors) : IZLinkSession
{
    private IZLinkSessionActor? _actor;

    public IZLinkSessionContext Context { get; } = context;

    public async ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        var actor = await actors.GetOrCreateAsync(
                "fixture",
                "hero",
                cancellationToken)
            .ConfigureAwait(false);

        _actor = await Context.Actors.BindAsync(
            actor,
            cancellationToken);
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        _actor = null;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        global::Systems.Zlink.Message payload,
        CancellationToken cancellationToken)
    {
        var actor = _actor ?? throw new InvalidOperationException("Actor is not bound.");
        return actor.RelayAsync(header, payload, cancellationToken);
    }
}

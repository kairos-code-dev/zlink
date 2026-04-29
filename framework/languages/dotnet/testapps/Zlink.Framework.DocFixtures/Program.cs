using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Protocol;
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
            options.UseDiscovery(discovery =>
            {
                discovery.Add("tcp://127.0.0.1:7100");
            });

            options.AddChannel("orders", channel =>
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
        builder.Services.AddZLinkHandlersFromAssemblyContaining<FixtureSendHandler>();
        return builder;
    }

    public static IHostApplicationBuilder CreateSpotBuilder()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddScoped<FixtureSpotTimerHandler>();
        builder.Services.AddScoped<FixtureSpotSubscriptionHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add("tcp://127.0.0.1:7300");
            });

            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:7301");
                spot.EnableRouter();
                spot.EnablePubSub();
                spot.AttachChannelClient("orders");
                spot.AttachSpotPublisherClient("game.stage");
                spot.AddSpotFactory<FixtureStageSpot>("stage");
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
                stream.AddHeaderSession<FixtureRawStreamSession>();
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

            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add("tcp://127.0.0.1:7602");
            });

            options.AddChannel("orders", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind("tcp://127.0.0.1:7603");
                });
            });

            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:7604");
                spot.EnableRouter();
                spot.EnablePubSub();
                spot.AddSpotFactory<FixtureStageSpot>("stage");
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
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add("tcp://127.0.0.1:7700");
            });

            options.AddActorFactory<FixtureActorFactory>("hero");

            options.AddStreamNode("stream.actor", stream =>
            {
                stream.Bind("tcp://127.0.0.1:7701");
                stream.AddHeaderSession<FixtureActorPacketSession>();
            });

            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:7702");
                spot.AddSpotFactory<FixtureActorSpot>("fixture-actor-stage");
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
        Context.AddSubscribe<FixtureSpotSubscriptionHandler>("stage.event");
    }

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        _ = await Context.AddTimer<FixtureSpotTimerHandler>(
            "heartbeat",
            TimeSpan.FromSeconds(1),
            cancellationToken);
    }
}

internal sealed class FixtureSpotTimerHandler(IZLinkSpotClient spotClient)
    : IZLinkSpotTimerHandler<FixtureStageSpot>
{
    public ValueTask HandleAsync(FixtureStageSpot spot, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        _ = spotClient.Publish("stage.event", new FixtureSpotEvent(spot.Context.SpotRid.ToHex())).Sync();
        return ValueTask.CompletedTask;
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

internal sealed class FixtureRawStreamSession : IZLinkSession
{
    public IZLinkSessionContext Context { get; set; } = default!;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
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
        global::Zlink.Message payload,
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
        Context.AddActorJoin<FixtureActorJoinHandler, FixtureActor, FixtureActorJoinRequest, FixtureActorJoinReply>();
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

internal sealed class FixtureActorFactory;

internal sealed class FixtureActor : IZLinkActor
{
    public string ActorId => "fixture";

    public FixtureActorSpot? Spot { get; private set; }

    public IZLinkActorContext Context { get; set; } = default!;

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

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        global::Zlink.Message body,
        CancellationToken cancellationToken)
    {
        _ = header;
        _ = body;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed class FixtureActorPacketSession(
    IZLinkSpotManager spotManager)
    : IZLinkSession
{
    private readonly FixtureActor _actor = new();

    public IZLinkSessionContext Context { get; set; } = default!;

    public async ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        await Context.AttachActorAsync(_actor, cancellationToken);
        var created = await spotManager.CreateAsync("fixture-actor-stage", cancellationToken);
        _ = await _actor.Context
            .JoinSpot(created.SpotRid, new FixtureActorJoinRequest("fixture-room"))
            .Async<FixtureActorJoinReply>(cancellationToken);
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        return Context.DisconnectActorAsync(cancellationToken);
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
        global::Zlink.Message body,
        CancellationToken cancellationToken)
    {
        return Context.DispatchToActorAsync(header, body, cancellationToken);
    }
}

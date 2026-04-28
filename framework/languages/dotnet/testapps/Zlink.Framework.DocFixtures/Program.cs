using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Headers;
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

internal sealed class FixtureStageSpot : ZLinkSpot
{
    public FixtureStageSpot(
        global::Zlink.RoutingId spotRid,
        global::Zlink.RoutingId nodeRid)
        : base(spotRid, nodeRid)
    {
        AddSubscribe<FixtureSpotSubscriptionHandler>("stage.event");
    }

    public override async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        _ = await AddTimer<FixtureSpotTimerHandler>(
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
        _ = spotClient.Publish("stage.event", new FixtureSpotEvent(spot.SpotRid.ToHex())).Exec();
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

internal sealed class FixtureRawStreamSession : IZLinkStreamHeaderSession
{
    public ValueTask OnConnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        _ = stream;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        _ = stream;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        IZLinkStream stream,
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = stream;
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        global::Zlink.Message payload,
        CancellationToken cancellationToken)
    {
        _ = stream;
        _ = header;
        _ = payload;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed class FixtureActorSpot : ZLinkSpot
{
    public FixtureActorSpot(
        global::Zlink.RoutingId spotRid,
        global::Zlink.RoutingId nodeRid)
        : base(spotRid, nodeRid)
    {
        AddActorJoin<FixtureActorJoinHandler, FixtureActor, FixtureActorJoinRequest, FixtureActorJoinReply>();
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
        await actor.OnAttachedAsync(spot, cancellationToken);
        return new FixtureActorJoinReply(request.RoomId);
    }
}

internal sealed record FixtureActorJoinRequest(string RoomId) : IZLinkRequest<FixtureActorJoinReply>;

internal sealed record FixtureActorJoinReply(string RoomId);

internal sealed class FixtureActorFactory;

internal sealed class FixtureActor : IZLinkActor
{
    public string ActorKey => "fixture";

    public ZLinkSpot? Spot { get; private set; }

    public ValueTask OnAttachedAsync(ZLinkSpot spot, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        Spot = spot;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDetachedAsync(ZLinkSpot spot, CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (ReferenceEquals(Spot, spot))
        {
            Spot = null;
        }

        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        IZLinkActorContext context,
        ZlinkStreamHeader header,
        global::Zlink.Message body,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = header;
        _ = body;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

internal sealed class FixtureActorPacketSession(
    IZLinkSpotManager spotManager,
    IZLinkActorRuntime actorRuntime)
    : IZLinkStreamHeaderSession
{
    private readonly FixtureActor _actor = new();

    public async ValueTask OnConnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        await actorRuntime.AttachAsync(_actor, stream, cancellationToken);
        var created = await spotManager.CreateAsync("fixture-actor-stage", cancellationToken);
        _ = await actorRuntime.JoinAsync<FixtureActorJoinRequest, FixtureActorJoinReply>(
            created.SpotRid,
            _actor,
            new FixtureActorJoinRequest("fixture-room"),
            cancellationToken);
    }

    public ValueTask OnDisconnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
    {
        return actorRuntime.DisconnectAsync(_actor, stream, cancellationToken);
    }

    public ValueTask OnErrorAsync(
        IZLinkStream stream,
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = stream;
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        global::Zlink.Message body,
        CancellationToken cancellationToken)
    {
        _ = stream;
        return actorRuntime.SubmitAsync(_actor, header, body, cancellationToken);
    }
}

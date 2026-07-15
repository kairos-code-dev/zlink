using Bingo.Server.Configuration;
using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Server.Play.Infrastructure.Redis;
using Bingo.Server.Play.Infrastructure.ZLink.Actors;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot.Notifications;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.EntrySpot;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using StackExchange.Redis;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Samples.Logging;

namespace Bingo.Server.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SamplePlayNode node,
        string nodeName,
        string logDirectory,
        bool enableMetrics = true)
    {
        var traceLabel = $"play-{nodeName}";
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
            traceLabel);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(node);
        builder.Services.AddSingleton<IConnectionMultiplexer>(_ =>
            ConnectionMultiplexer.Connect(topology.RedisEndpoint));
        builder.Services.AddSingleton<IBingoMatchQueue, RedisBingoMatchQueue>();
        builder.Services.AddSingleton<BingoRoomAllocator>();
        builder.Services.AddSingleton<BingoRoomEventMapper>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();
        if (enableMetrics) builder.Services.AddBingoMetrics();

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, traceLabel))
                .TraceLabel(traceLabel);
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();

            const string allocationGroup = "bingo.play";
            options.AddClientServerChannel(SampleNames.PlayChannel)
                .UseAllocatedRoutingId(slotCount: 2, routingIdPrefix: "play")
                .SetRoutingIdAllocationGroup(allocationGroup)
                .EnableServer(node.PlayChannelEndpoint)
                .EnableClient()
                .AddHandlerGroup("play");
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery)
                .UseDrainPolicy(ZLinkSpotDrainPolicy.DrainNatural)
                .UseAllocatedRoutingId(slotCount: 2, routingIdPrefix: "play")
                .SetRoutingIdAllocationGroup(allocationGroup)
                .EnableRouter(node.SpotRouterEndpoint)
                .EnablePubSub(node.SpotPubEndpoint)
                .AddEntrySpot<BingoEntrySpot>()
                .AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType)
                .AddActorTransferAdapter<PlayerActor, PlayerActorTransferAdapter>(SampleNames.PlayerActorType)
                .AddSpotFactory<BingoRoom>();
        });
        builder.Services.AddSingleton(new BingoRoutingIdReport(
            "play",
            "bingo.play",
            [SampleNames.PlayChannel, SampleNames.RoomSpotDiscovery]));
        builder.Services.AddHostedService<BingoRoutingIdReporter>();

        return builder.Build();
    }
}

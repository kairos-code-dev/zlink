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
        string logDirectory,
        bool enableMetrics = true)
    {
        var traceLabel = $"play-{node.NodeRid}";
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

            options.AddClientServerChannel(SampleNames.PlayChannel)
                .EnableServer(node.PlayChannelEndpoint)
                .EnableClient()
                .SetRoutingId(node.NodeRid)
                .AddHandlerGroup("play");
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery)
                .UseDrainPolicy(ZLinkSpotDrainPolicy.DrainNatural)
                .EnableRouter(node.SpotRouterEndpoint)
                .SetRoutingId(node.NodeRid)
                .SetEntrySpotRoutingId(node.NodeRid)
                .EnablePubSub(node.SpotPubEndpoint)
                .AddEntrySpot<BingoEntrySpot>()
                .AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType)
                .AddActorTransferAdapter<PlayerActor, PlayerActorTransferAdapter>(SampleNames.PlayerActorType)
                .AddSpotFactory<BingoRoom>();
        });

        return builder.Build();
    }
}

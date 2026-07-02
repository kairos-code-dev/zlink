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
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace Bingo.Server.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(SampleTopology topology, SamplePlayNode node)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("BINGO_LOG_DIR"),
            "play");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(node);
        builder.Services.AddSingleton<IConnectionMultiplexer>(_ =>
            ConnectionMultiplexer.Connect(topology.RedisEndpoint));
        builder.Services.AddSingleton<IBingoMatchQueue, RedisBingoMatchQueue>();
        builder.Services.AddSingleton<BingoRoomAllocator>();
        builder.Services.AddSingleton<BingoRoomEventMapper>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("play"))
                .TraceLabel("play");
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();

            options.AddRouteMesh(SampleNames.PlayChannel)
                .EnableServer(node.PlayChannelEndpoint)
                .EnableClient()
                .SetRoutingId(node.NodeRid)
                .AddHandlerGroup("play");
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery)                .EnableRouter(node.SpotRouterEndpoint)
                .SetRoutingId(node.NodeRid)
                .EnablePubSub(node.SpotPubEndpoint)
                .AddEntrySpot<BingoEntrySpot>()
                .AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType)
                .AddSpotFactory<BingoRoom>();
        });

        return builder.Build();
    }
}

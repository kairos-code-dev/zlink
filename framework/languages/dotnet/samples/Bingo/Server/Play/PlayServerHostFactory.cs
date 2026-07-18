using Microsoft.Extensions.Configuration;

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
        SampleRuntimeConfiguration<SamplePlayNode> configuration,
        bool enableMetrics = true)
    {
        var node = configuration.Node;
        var nodeName = configuration.NodeName;
        var logDirectory = configuration.LogDirectory;
        var traceLabel = $"play-{nodeName}";
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
            traceLabel);
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(node);
        builder.Services.AddSingleton<IConnectionMultiplexer>(_ =>
            ConnectionMultiplexer.Connect(configuration.RedisEndpoint));
        builder.Services.AddSingleton<IBingoMatchQueue, RedisBingoMatchQueue>();
        builder.Services.AddSingleton<BingoRoomAllocator>();
        builder.Services.AddSingleton<BingoRoomEventMapper>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();
        if (enableMetrics) builder.Services.AddBingoMetrics();

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(configuration.RedisEndpoint)
                .SetKeyPrefix(configuration.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, traceLabel))
                .TraceLabel(traceLabel);
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();

            options.AddRouteMesh(SampleNames.RoomSpotDiscovery)
                .UseDrainPolicy(ZLinkMeshNodeDrainPolicy.DrainNatural)
                .UseAllocatedRoutingId(slotCount: 2, routingIdPrefix: "play")
                .SetRoutingIdAllocationGroup(SampleNames.PlayAllocationGroup)
                .Listen(node.SpotRouterEndpoint)
                .AddEntrySpot<BingoEntrySpot>()
                .AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType)
                .AddActorTransferAdapter<PlayerActor, PlayerActorTransferAdapter>(SampleNames.PlayerActorType)
                .AddSpotFactory<BingoRoom>();
        });
        builder.Services.AddSingleton(new BingoRoutingIdReport(
            "play",
            SampleNames.PlayAllocationGroup,
            [SampleNames.RoomSpotDiscovery]));
        builder.Services.AddHostedService<BingoRoutingIdReporter>();

        return builder.Build();
    }
}

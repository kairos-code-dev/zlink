using Bingo.Server.Play.Infrastructure.ZLink.Actors;
using Bingo.Server.Play.Infrastructure.Redis;
using Bingo.Server.Play.Infrastructure.ZLink.Notifications;
using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Server.Play.Infrastructure.ZLink.Spots;
using Bingo.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using StackExchange.Redis;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;

namespace Bingo.Server.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(SampleTopology topology, SamplePlayNode node)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(node);
        builder.Services.AddSingleton<IConnectionMultiplexer>(_ => ConnectionMultiplexer.Connect(topology.RedisEndpoint));
        builder.Services.AddSingleton<IBingoMatchQueue, RedisBingoMatchQueue>();
        builder.Services.AddSingleton<BingoRoomAllocator>();
        builder.Services.AddSingleton<BingoRoomEventMapper>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .SetMessageDispatchErrorObserver<BingoDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("play"))
                .TraceNodeId("play");
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();
            options.AddRouteMesh(SampleNames.PlayChannel)
                .EnableServer(node.PlayChannelEndpoint)
                .EnableClient()
                .SetRoutingId(node.NodeRid)
                .AddHandlerGroup("play");
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery).UseRegistrySpotResolver()
                .EnableRouter(node.SpotRouterEndpoint)
                .SetRouterRoutingId(node.NodeRid)
                .EnablePubSub(node.SpotPubEndpoint)
                .SetPubSubRoutingId(node.NodeRid)
                .AddEntrySpot<BingoEntrySpot>()
                .AddSpotFactory<BingoRoom>();
        });

        return builder.Build();
    }
}

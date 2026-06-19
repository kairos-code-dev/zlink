using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Adapters.ZLink.Notifications;
using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Server.Play.Adapters.ZLink.Spots;
using Bingo.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using StackExchange.Redis;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;

namespace Bingo.Server.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(SampleTopology topology, SamplePlayNode node)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(node);
        builder.Services.AddSingleton<IConnectionMultiplexer>(_ => ConnectionMultiplexer.Connect(topology.RedisEndpoint));
        builder.Services.AddSingleton<RedisBingoMatchQueue>();
        builder.Services.AddSingleton<BingoRoomAllocator>();
        builder.Services.AddSingleton<BingoRoomEventMapper>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.UseRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery).RouterChannelId = SampleNames.RoomSpotNode;
            {
                var channel = options.AddRouteMeshChannel(SampleNames.PlayChannel);
                channel.EnableServer(node.PlayChannelEndpoint);
                channel.EnableClient(node.PeerPlayChannelEndpoint);
                channel.ConfigureSocket().SendTimeout = TimeSpan.FromSeconds(1);
                channel.ConfigureRouting().RoutingId = node.NodeRid;
                channel.AddHandlerGroup("play");

            }
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            {
                var spotMesh = options.AddSpotMesh(SampleNames.RoomSpotDiscovery);
                spotMesh.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
                {
                    var spot = spotMesh.AddNode(SampleNames.RoomSpotNode);
                    {
                        var router = spot.EnableRouter(node.SpotRouterEndpoint);
                        router.SetRouterRoutingId(node.NodeRid);

                    }
                    {
                        var pubsub = spot.EnablePubSub(node.SpotPubEndpoint);
                        pubsub.SetPubSubRoutingId(node.NodeRid);

                    }
                    spot.AcceptSpotRoutesFromChannel(SampleNames.PlayChannel, node.PlayChannelEndpoint);
                    spot.AttachChannelClient(SampleNames.ApiChannel, topology.ApiA.ChannelEndpoint);
                    spot.AttachChannelClient(SampleNames.ApiChannel, topology.ApiB.ChannelEndpoint);
                    spot.AddEntrySpot<BingoEntrySpot>();
                    spot.AddSpotFactory<BingoRoom>();

                }

            }
        });

        return builder.Build();
    }
}

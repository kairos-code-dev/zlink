using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Adapters.ZLink.Notifications;
using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Server.Play.Adapters.ZLink.Spots;
using Bingo.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Bingo.Server.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<BingoRoomAllocator>();
        builder.Services.AddSingleton<BingoRoomEventMapper>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.AddProtobuf();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            {
                var channel = options.AddClientServerChannel(SampleNames.PlayChannel);
                channel.EnableServer(topology.PlayChannelEndpoint);
                channel.AddHandlerGroup("play");

            }
            {
                var channel = options.AddClientServerChannel(SampleNames.ApiChannel);
                channel.EnableClient();

            }
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            {
                var spotMesh = options.AddSpotMesh(SampleNames.RoomSpotDiscovery);
                {
                    var spot = spotMesh.AddNode(SampleNames.RoomSpotNode);
                    {
                        var router = spot.EnableRouter(topology.PlaySpotRouterEndpoint);
                        router.SetRouterRoutingId(topology.PlayRid);

                    }
                    {
                        var pubsub = spot.EnablePubSub(topology.PlaySpotEndpoint);

                    }
                    spot.AttachChannelClient(SampleNames.ApiChannel);
                    spot.AddEntrySpot<BingoEntrySpot>();
                    spot.AddSpotFactory<BingoRoom>();

                }

            }
        });

        return builder.Build();
    }
}

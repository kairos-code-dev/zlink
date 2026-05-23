using Bingo.Server.Play.Actors;
using Bingo.Server.Play.BingoRoomSpots;
using Bingo.Server.Play.EntrySpot;
using Bingo.Shared.Configuration;
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
        builder.Services.AddSingleton<BingoRoomDirectory>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddClientServerChannel(SampleNames.PlayChannel, channel =>
            {
                channel.EnableServer(server => server.Bind(topology.PlayChannelEndpoint));
                channel.AddHandlerGroup("play");
            });
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery, spotMesh =>
            {
                spotMesh.AddNode(SampleNames.RoomSpotNode, spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind(topology.PlaySpotRouterEndpoint);
                        router.SetRoutingId(topology.PlayRid);
                    });
                    spot.EnablePubSub(pubsub =>
                    {
                        pubsub.SetPubBind(topology.PlaySpotEndpoint);
                    });
                    spot.AttachClientServerChannelClient(SampleNames.ApiChannel);
                    spot.AddEntrySpot<BingoEntrySpot>();
                    spot.AddSpotFactory<BingoRoomSpot>(SampleNames.RoomSpotType);
                });
            });
        });

        return builder.Build();
    }
}

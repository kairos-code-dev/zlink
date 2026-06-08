using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Adapters.ZLink.Handlers;
using Bingo.Server.Play.Adapters.ZLink.Notifications;
using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Server.Play.Adapters.ZLink.Spots;
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
        builder.Services.AddSingleton<BingoRoomAllocator>();
        builder.Services.AddSingleton<BingoNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
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
                        router.BindRouter(topology.PlaySpotRouterEndpoint);
                        router.SetRoutingId(topology.PlayRid);
                    });
                    spot.EnablePubSub(pubsub =>
                    {
                        pubsub.BindPubSub(topology.PlaySpotEndpoint);
                    });
                    spot.AttachChannelClient(SampleNames.ApiChannel);
                    spot.AddEntrySpot<BingoEntrySpot>();
                    spot.AddSpotFactory<BingoRoom>();
                });
            });
        });

        return builder.Build();
    }
}

using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Server.Play.EntrySpot;
using TicTacToe.SessionGateway.Server.Play.GameSpots;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionGateway.Server.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<GameNotificationPublisher>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf(typeof(PlayServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddClientServerChannel(SampleNames.PlayChannel, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(topology.PlayChannelEndpoint);
                });
                channel.AddHandlerGroup("play");
            });
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            options.UseRegistrySpotRemoteAddresses("tictactoe");
            options.AddRouteMeshChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(topology.PlayRouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = topology.PlayRid);
                routed.UseManualConnections(connections =>
                {
                    connections.Connect(topology.SessionSpotEndpoint);
                    connections.Connect(topology.ReconnectSessionSpotEndpoint);
                });
            });
            options.AddSpotMesh(SampleNames.GameSpotDiscovery, spotMesh =>
            {
                spotMesh.AddNode(SampleNames.GameSpotNode, spot =>
                {
                    spot.ConfigureEntrySpot(entry =>
                    {
                        entry.RoutingId = RoutingId.FromString(SampleNames.EntrySpotRoutingId);
                    });
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind(topology.PlaySpotRouterEndpoint);
                        router.SetRoutingId(topology.PlayRid);
                        router.UseManualConnections(connections =>
                        {
                            connections.Connect(topology.SessionRouterEndpoint);
                            connections.Connect(topology.ReconnectSessionRouterEndpoint);
                        });
                    });
                    spot.AcceptSpotRoutesFromChannel(SampleNames.RouterChannel);
                    spot.AddEntrySpot<TicTacToeEntrySpot>();
                    spot.AddSpotFactory<TicTacToeGameSpot>();
                });
            });
        });

        return builder.Build();
    }
}

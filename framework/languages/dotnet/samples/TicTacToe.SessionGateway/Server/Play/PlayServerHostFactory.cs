using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Play;
using TicTacToe.SessionGateway.Infrastructure;
using TicTacToe.SessionGateway.Infrastructure.Configuration;
using TicTacToe.SessionGateway.Play.EntrySpot;
using TicTacToe.SessionGateway.Play.GameSpots;
using TicTacToe.SessionGateway.Shared.Configuration;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionGateway.Play;

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
            options.UseRegistrySpotRoutes("tictactoe");
            options.AddRouteMeshChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(topology.PlayRouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = topology.PlayRid);
            });
            options.AddSpotMesh(SampleNames.GameSpotDiscovery, spotMesh =>
            {
                spotMesh.UseDiscovery(discovery =>
                {
                    discovery.Add(topology.RegistryRouterEndpoint);
                });
                spotMesh.AddNode(SampleNames.GameSpotNode, spot =>
                {
                    spot.Bind(topology.PlaySpotEndpoint);
                    spot.EnableRouter();
                    spot.AcceptSpotRoutesFromChannel(SampleNames.RouterChannel);
                    spot.AddEntrySpot<TicTacToeEntrySpot>();
                    spot.AddSpotFactory<TicTacToeGameSpot>(SampleNames.GameSpotType);
                });
            });
        });

        return builder.Build();
    }
}

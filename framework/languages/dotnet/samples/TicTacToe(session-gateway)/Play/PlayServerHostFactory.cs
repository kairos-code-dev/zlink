using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Infrastructure;
using TicTacToe.SessionActorDispatch.Play;
using TicTacToe.SessionGateway.Infrastructure;
using TicTacToe.SessionGateway.Infrastructure.Configuration;
using TicTacToe.SessionGateway.Shared.Configuration;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionGateway.Play;

public static class PlayServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        RegistryActorSessionLocationStore sessionLocations,
        RegistryPlayRouteStore playRoutes)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(sessionLocations);
        builder.Services.AddSingleton(playRoutes);
        builder.Services.AddSingleton<ISpotRouteResolver>(playRoutes);
        builder.Services.AddSingleton<ISpotRouteWriter>(playRoutes);
        builder.Services.AddSingleton<RegistryPlayRoutePublisher>();
        builder.Services.AddSingleton<GameNotificationPublisher>();
        builder.Services.AddScoped<PlayerActorFactory>();
        builder.Services.AddScoped<TicTacToeEntrySpot>();
        builder.Services.AddScoped<EnsurePlayerActorHandler>();
        builder.Services.AddScoped<CreateMatchRoomHandler>();
        builder.Services.AddScoped<JoinMatchHandler>();
        builder.Services.AddScoped<TicTacToeGameJoinHandler>();
        builder.Services.AddScoped<PlaceMarkHandler>();
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
            });
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            options.AddActorPlayRouteResolver<RegistryPlayRouteStore>();
            options.AddSpotRouteResolver<RegistryPlayRouteStore>();
            options.AddActorSessionBindingStore<RegistryActorSessionLocationStore>();
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
                    spot.AddEntrySpot<TicTacToeEntrySpot>();
                    spot.AddSpotFactory<TicTacToeGameSpot>(SampleNames.GameSpotType);
                });
            });
        });

        return builder.Build();
    }
}

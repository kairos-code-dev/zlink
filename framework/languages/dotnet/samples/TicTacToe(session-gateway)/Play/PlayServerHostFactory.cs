using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionActorDispatch.Play;

internal static class PlayServerHostFactory
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
        builder.Services.AddSingleton<RegistryPlayRoutePublisher>();
        builder.Services.AddSingleton<GameNotificationPublisher>();
        builder.Services.AddScoped<PlayerActorFactory>();
        builder.Services.AddScoped<CreateMatchRoomHandler>();
        builder.Services.AddScoped<JoinMatchHandler>();
        builder.Services.AddScoped<TicTacToeGameJoinHandler>();
        builder.Services.AddScoped<PlaceMarkHandler>();
        builder.Services.AddZLinkHandlersFromAssemblyContaining<CreateMatchRoomHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.UseSpotDiscovery(SampleNames.GameSpotDiscovery, discovery =>
            {
                discovery.Add(topology.RegistryRouterEndpoint);
            });
            options.AddChannel(SampleNames.PlayChannel, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(topology.PlayChannelEndpoint);
                });
            });
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            options.AddActorPlayRouteResolver<RegistryPlayRouteStore>();
            options.AddActorSessionRouteResolver<RegistryActorSessionLocationStore>();
            options.AddRoutedChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(topology.PlayRouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = topology.PlayRid);
            });
            options.AddSpotNode(SampleNames.GameSpotNode, spot =>
            {
                spot.Bind(topology.PlaySpotEndpoint);
                spot.AddSpotFactory<TicTacToeGameSpot>(SampleNames.GameSpotType);
            });
        });

        return builder.Build();
    }
}

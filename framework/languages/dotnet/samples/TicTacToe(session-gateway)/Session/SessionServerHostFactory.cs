using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.SessionActorDispatch.Session;

internal static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode sessionNode,
        RegistryActorSessionLocationStore sessionLocations,
        RegistryPlayRouteStore playRoutes)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(sessionLocations);
        builder.Services.AddSingleton(playRoutes);
        builder.Services.AddSingleton<IInitialActorRouteResolver>(
            new StaticInitialActorRouteResolver(new ZLinkPlayRoute(SampleNames.RouterChannel, topology.PlayRid)));
        builder.Services.AddSingleton<SessionActorRouteCache>();
        builder.Services.AddScoped<ISessionRelayPacketHandler, AuthenticateSessionPacketHandler>();
        builder.Services.AddScoped<ISessionRelayPacketHandler, CreateMatchSessionPacketHandler>();
        builder.Services.AddScoped<ISessionRelayPacketHandler, JoinMatchSessionPacketHandler>();
        builder.Services.AddScoped<ISessionRelayPacketHandler, PlaceMarkSessionPacketHandler>();
        builder.Services.AddScoped<SessionRelaySession>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddActorSessionLocationWriter<RegistryActorSessionLocationStore>();
            options.AddChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddRoutedChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(sessionNode.RouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = sessionNode.RoutingId);
            });
            options.AddStreamNode(SampleNames.StreamNode, stream =>
            {
                stream.Bind(sessionNode.StreamEndpoint);
                stream.AddHeaderSession<SessionRelaySession>();
            });
        });

        return builder.Build();
    }
}

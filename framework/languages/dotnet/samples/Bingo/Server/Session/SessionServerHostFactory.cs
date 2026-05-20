using Bingo.Server.Infrastructure;
using Bingo.Server.Infrastructure.Configuration;
using Bingo.Shared.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Bingo.Server.Session;

public static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode session)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<SessionActorRouteCache>();
        builder.Services.AddScoped<ISessionRelayPacketHandler, AuthenticateSessionPacketHandler>();
        builder.Services.AddScoped<ISessionRelayPacketHandler, MatchBingoSessionPacketHandler>();
        builder.Services.AddScoped<ISessionRelayPacketHandler, StartBingoSessionPacketHandler>();
        builder.Services.AddScoped<SessionRelaySession>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.UseSpotDiscovery(SampleNames.RoomSpotDiscovery, discovery =>
            {
                discovery.Add(topology.RegistryRouterEndpoint);
            });
            options.UseRegistryActorSessionBindings("bingo");
            options.UseRegistryActorRoutes("bingo");
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddClientServerChannel(SampleNames.PlayChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddRouteMeshChannel(SampleNames.RouterChannel, routed =>
            {
                routed.Bind(session.RouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = session.RoutingId);
            });
            options.AddStreamNode(SampleNames.StreamNode, stream =>
            {
                stream.Bind(session.StreamEndpoint);
                stream.AddHeaderSession<SessionRelaySession>();
            });
        });

        return builder.Build();
    }
}

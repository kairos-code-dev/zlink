using Bingo.Server.Session.Sessions;
using Bingo.Server.Session.Sessions.Handlers;
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
        builder.Services.AddScoped<IBingoSessionHandler, AuthenticateBingoSessionHandler>();
        builder.Services.AddScoped<IBingoSessionHandler, MatchBingoBingoSessionHandler>();
        builder.Services.AddScoped<IBingoSessionHandler, StartBingoBingoSessionHandler>();
        builder.Services.AddScoped<Sessions.BingoSession>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.UseSpotDiscovery(SampleNames.RoomSpotDiscovery, discovery =>
            {
                discovery.Add(topology.RegistryRouterEndpoint);
            });
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
                stream.AddHeaderSession<Sessions.BingoSession>();
            });
        });

        return builder.Build();
    }
}

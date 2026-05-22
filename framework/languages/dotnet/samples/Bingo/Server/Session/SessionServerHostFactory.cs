using Bingo.Server.Session.Sessions;
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
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddClientServerChannel(SampleNames.PlayChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery, mesh =>
            {
                mesh.AddNode(SampleNames.SessionSpotNode, node =>
                {
                    node.Bind(session.SpotEndpoint);
                    node.EnableRouter(router =>
                    {
                        router.Bind(session.RouterEndpoint);
                        router.ConfigureRouting(routing => routing.RoutingId = session.RoutingId);
                    });
                });
            });
            options.AddStreamNode(SampleNames.StreamNode, stream =>
            {
                stream.AttachActorGateway(SampleNames.SessionSpotNode);
                stream.Bind(session.StreamEndpoint);
                stream.RegisterSession<Sessions.BingoSession>();
            });
        });

        return builder.Build();
    }
}

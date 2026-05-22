using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Infrastructure.Configuration;
using TicTacToe.SessionGateway.Shared.Configuration;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

public static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode sessionNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
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
            options.AddSpotMesh(SampleNames.GameSpotDiscovery, mesh =>
            {
                mesh.AddNode(SampleNames.SessionSpotNode, node =>
                {
                    node.Bind(sessionNode.SpotEndpoint);
                    node.EnableRouter(router =>
                    {
                        router.Bind(sessionNode.RouterEndpoint);
                        router.ConfigureRouting(routing => routing.RoutingId = sessionNode.RoutingId);
                    });
                });
            });
            options.AddStreamNode(SampleNames.StreamNode, stream =>
            {
                stream.AttachActorGateway(SampleNames.SessionSpotNode);
                stream.Bind(sessionNode.StreamEndpoint);
                stream.RegisterSession<SessionRelaySession>();
            });
        });

        return builder.Build();
    }
}

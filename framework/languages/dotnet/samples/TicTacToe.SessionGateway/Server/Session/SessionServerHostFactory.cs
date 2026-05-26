using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Server.Session.Sessions;
using TicTacToe.SessionGateway.Session.Sessions;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Shared.Configuration;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionGateway.Server.Session;

public static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode sessionNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddZLinkFramework(options =>
        {
            options.Codecs.AddJson();
            options.AddActorFactory<PlayerActorFactory>(SampleNames.PlayerActorType);
            options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
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
                routed.Bind(sessionNode.SpotEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = sessionNode.RoutingId);
                routed.UseManualConnections(connections =>
                {
                    connections.Connect(topology.PlayRouterEndpoint);
                });
            });
            options.AddSpotMesh(SampleNames.GameSpotDiscovery, mesh =>
            {
                mesh.AddNode(SampleNames.SessionSpotNode, node =>
                {
                    node.EnableRouter(router =>
                    {
                        router.SetRouterBind(sessionNode.RouterEndpoint);
                        router.SetRoutingId(sessionNode.RoutingId);
                        router.UseManualConnections(connections =>
                        {
                            connections.Connect(topology.PlaySpotRouterEndpoint);
                        });
                    });
                    node.AcceptSpotRoutesFromChannel(SampleNames.RouterChannel);
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

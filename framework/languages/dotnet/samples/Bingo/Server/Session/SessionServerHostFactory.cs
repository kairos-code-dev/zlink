using Systems.Zlink;
using Systems.Zlink.Codecs.Protobuf;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
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
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.AddProtobuf();
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
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
                    node.EnableRouter(router =>
                    {
                        router.BindRouter(session.RouterEndpoint);
                        router.SetRoutingId(session.RouterRoutingId);
                    });
                    node.EnablePubSub(pubsub =>
                    {
                        pubsub.BindPubSub(session.PubEndpoint);
                        pubsub.SetRoutingId(session.PubRoutingId);
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

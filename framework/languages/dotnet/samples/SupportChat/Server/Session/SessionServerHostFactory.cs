using SupportChat.Server.Session.Sessions;
using SupportChat.Shared.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace SupportChat.Server.Session;

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
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddClientServerChannel(SampleNames.SupportChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddSpotMesh(SampleNames.SupportSpotDiscovery, mesh =>
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
                stream.RegisterSession<SupportChatSession>();
            });
        });

        return builder.Build();
    }
}

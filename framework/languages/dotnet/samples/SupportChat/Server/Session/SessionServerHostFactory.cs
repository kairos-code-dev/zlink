using SupportChat.Server.Session.Sessions;
using SupportChat.Server.Configuration;
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
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<SupportChatDispatchErrorObserver>();
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            {
                var channel = options.AddClientServerChannel(SampleNames.ApiChannel);
                channel.EnableClient();

            }
            {
                var channel = options.AddClientServerChannel(SampleNames.SupportChannel);
                channel.EnableClient();

            }
            {
                var mesh = options.AddSpotMesh(SampleNames.SupportSpotDiscovery);
                {
                    var node = mesh.AddNode(SampleNames.SessionSpotNode);
                    {
                        var router = node.EnableRouter(session.RouterEndpoint);
                        router.SetRouterRoutingId(session.RouterRoutingId);

                    }
                    {
                        var pubsub = node.EnablePubSub(session.PubEndpoint);
                        pubsub.SetPubSubRoutingId(session.PubRoutingId);

                    }

                }

            }
            {
                var stream = options.AddStreamNode(SampleNames.StreamNode);
                stream.AttachActorGateway(SampleNames.SessionSpotNode);
                stream.Bind(session.StreamEndpoint);
                stream.RegisterSession<SupportChatSession>();

            }
        });

        return builder.Build();
    }
}

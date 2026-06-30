using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using SupportChat.Server.Configuration;
using SupportChat.Server.Session.Sessions;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

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
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("session"))
                .TraceLabel("session");
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
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
                    var node = mesh;
                    {
                        var router = node.EnableRouter(session.RouterEndpoint);
                        router.SetRoutingId(session.RoutingId);
                    }
                    {
                        var pubsub = node.EnablePubSub(session.PubEndpoint);
                    }
                }
            }
            {
                var stream = options.AddStreamNode(SampleNames.StreamNode);
                stream.Bind(session.StreamEndpoint);
                stream.RegisterSession<SupportChatSession>();
            }
        });

        return builder.Build();
    }
}
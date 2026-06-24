using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Infrastructure.ZLink.Notifications;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots;
using SupportChat.Server.Support.Infrastructure.ZLink;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace SupportChat.Server.Support;

public static class SupportServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<IConversationStarter, ConversationStarter>();
        builder.Services.AddSingleton<SupportConversationAllocator>();
        builder.Services.AddSingleton<AgentAvailabilityDirectory>();
        builder.Services.AddSingleton<AgentAssignmentService>();
        builder.Services.AddSingleton<SupportActorDirectory>();
        builder.Services.AddSingleton<ConversationNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .SetMessageDispatchErrorObserver<SupportChatDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("support"))
                .TraceNodeId("support");
            options.AddHandlersFromAssemblyOf(typeof(SupportServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            {
                var channel = options.AddClientServerChannel(SampleNames.SupportChannel);
                channel.EnableServer(topology.SupportChannelEndpoint);
                channel.AddHandlerGroup("support");

            }
            {
                var channel = options.AddClientServerChannel(SampleNames.ApiChannel);
                channel.EnableClient();

            }
            options.AddActorFactory<SupportUserActorFactory>(SampleNames.SupportActorType);
            {
                var mesh = options.AddSpotMesh(SampleNames.SupportSpotDiscovery);
                {
                    var spot = mesh;
                    {
                        var router = spot.EnableRouter(topology.SupportEntrySpotRouterEndpoint);
                        router.SetRouterRoutingId(topology.SupportEntryRid);

                    }
                    {
                        var pubsub = spot.EnablePubSub(topology.SupportEntrySpotEndpoint);

                    }
                    spot.AddEntrySpot<SupportEntrySpot>();
                    spot.AddSpotFactory<ConversationSpot>();

                }

            }
        });

        return builder.Build();
    }
}

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Support.Infrastructure.ZLink;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Notifications;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.EntrySpot;
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
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("support"))
                .TraceLabel("support");
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
            {
                var mesh = options.AddSpotMesh(SampleNames.SupportSpotDiscovery);
                {
                    var spot = mesh;
                    {
                        var router = spot.EnableRouter(topology.SupportEntrySpotRouterEndpoint);
                        router.SetRoutingId(topology.SupportEntryRid);
                    }
                    {
                        var pubsub = spot.EnablePubSub(topology.SupportEntrySpotEndpoint);
                    }
                    spot.AddEntrySpot<SupportEntrySpot>();
                    spot.AddActorFactory<SupportUserActorFactory>(SampleNames.SupportActorType);
                    spot.AddSpotFactory<ConversationSpot>();
                }
            }
        });

        return builder.Build();
    }
}
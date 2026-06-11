using SupportChat.Server.Support.Adapters.ZLink.Actors;
using SupportChat.Server.Support.Adapters.ZLink.Notifications;
using SupportChat.Server.Support.Adapters.ZLink.Spots;
using SupportChat.Server.Support.Adapters.ZLink;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace SupportChat.Server.Support;

public static class SupportServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<SupportConversationAllocator>();
        builder.Services.AddSingleton<AgentAvailabilityDirectory>();
        builder.Services.AddSingleton<AgentAssignmentService>();
        builder.Services.AddSingleton<SupportActorDirectory>();
        builder.Services.AddSingleton<ConversationNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(SupportServerHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
            options.AddClientServerChannel(SampleNames.SupportChannel, channel =>
            {
                channel.EnableServer(server => server.Bind(topology.SupportChannelEndpoint));
                channel.AddHandlerGroup("support");
            });
            options.AddClientServerChannel(SampleNames.ApiChannel, channel =>
            {
                channel.EnableClient();
            });
            options.AddActorFactory<SupportUserActorFactory>(SampleNames.SupportActorType);
            options.AddSpotMesh(SampleNames.SupportSpotDiscovery, mesh =>
            {
                mesh.AddNode(SampleNames.SupportSpotNode, spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(topology.SupportEntrySpotRouterEndpoint);
                        router.SetRoutingId(topology.SupportEntryRid);
                    });
                    spot.EnablePubSub(pubsub =>
                    {
                        pubsub.BindPubSub(topology.SupportEntrySpotEndpoint);
                    });
                    spot.AttachChannelClient(SampleNames.ApiChannel);
                    spot.AddEntrySpot<SupportEntrySpot>();
                });
                mesh.AddNode(SampleNames.SupportConversationSpotNode, spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(topology.SupportConversationSpotRouterEndpoint);
                    });
                    spot.EnablePubSub(pubsub =>
                    {
                        pubsub.BindPubSub(topology.SupportConversationSpotEndpoint);
                    });
                    spot.AddSpotFactory<ConversationSpot>();
                });
            });
        });

        return builder.Build();
    }
}

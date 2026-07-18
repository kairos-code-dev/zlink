using Systems.Zlink;
using Microsoft.Extensions.Configuration;

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
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace SupportChat.Server.Support;

public static class SupportServerHostFactory
{
    public static IHost Build(SampleTopology topology, string logDirectory)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
            "support");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<IConversationStarter, ConversationStarter>();
        builder.Services.AddSingleton<SupportConversationAllocator>();
        builder.Services.AddSingleton(new AgentAvailabilityDirectory(SampleNames.AgentCapacity));
        builder.Services.AddSingleton<AgentAssignmentService>();
        builder.Services.AddSingleton<SupportActorDirectory>();
        builder.Services.AddSingleton<ConversationNotificationPublisher>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, "support"))
                .TraceLabel("support");
            options.AddHandlersFromAssemblyOf(typeof(SupportServerHostFactory));
            options.AddClientServerChannel(SampleNames.SupportChannel)
                .EnableServer(topology.SupportChannelEndpoint)
                // Discovery clients dial this server through its descriptor
                // row, which needs a concrete routing id to be advertised.
                .SetRoutingId(RoutingId.From("2101"))
                .AddHandlerGroup("support");
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();
            var mesh5 = options.AddRouteMesh(SampleNames.SupportSpotDiscovery)
                .Listen(topology.SupportEntrySpotRouterEndpoint)
                .SetRoutingId(topology.SupportEntryRid)
                .AddEntrySpot<SupportEntrySpot>()
                .AddActorFactory<SupportUserActorFactory>(SampleNames.SupportActorType)
                .AddActorTransferAdapter<SupportUserActor, SupportUserActorTransferAdapter>(SampleNames.SupportActorType)
                .AddSpotFactory<ConversationSpot>();
            mesh5.ChannelName(SampleNames.SupportSpotDiscovery);
        });

        return builder.Build();
    }
}

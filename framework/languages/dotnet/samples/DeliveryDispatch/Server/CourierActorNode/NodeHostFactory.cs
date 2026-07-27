using Microsoft.Extensions.Configuration;

using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CourierActorNode;

public static class NodeHostFactory
{
    public static IHost Build(SampleConfiguration configuration)
    {
        var topology = configuration.Topology;
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            configuration.Role.LogDir,
            configuration.Role.Name);
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<ActorDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(configuration.FlowLogPath)
                .TraceLabel(configuration.Role.Name);
            options.AddHandlersFromAssemblyOf(typeof(NodeHostFactory));
            var mesh = options.AddRouteMesh(SampleNames.MeshName)
                .Listen(topology.MeshEndpoint)
                .SetRoutingIdPrefix("courier-actor")
                .AddEntrySpot<CourierEntrySpot>()
                .AddActorFactory<CourierActorFactory>(SampleNames.CourierActorType);
            mesh.ChannelName(SampleNames.MeshName).SetWeight(0);
            // The courier's decision goes back to dispatch as its own one-way message, so this
            // node needs a way to speak to the dispatch channel (common sample spec §7.4).
            mesh.ChannelName(SampleNames.DispatchChannel).SetWeight(0);
            mesh.ChannelName(SampleNames.TrackingRouteChannel).SetWeight(0);
        });

        return builder.Build();
    }

}

using Microsoft.Extensions.Configuration;

using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.Tracking;

public static class TrackingServerHostFactory
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
            "tracking");
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<EvidenceStore>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(configuration.FlowLogPath)
                .TraceLabel("tracking");
            options.AddHandlersFromAssemblyOf(typeof(DeliveryStatusChangedHandler));
            var mesh = options.AddRouteMesh(SampleNames.MeshName)
                .Listen(topology.MeshEndpoint)
                .SetRoutingIdPrefix("delivery-tracking");
            mesh.ChannelName(SampleNames.TrackingRouteChannel)
                .AddHandlerGroup(SampleNames.TrackingRouteChannel);
            mesh.ChannelName(SampleNames.DispatchChannel).SetWeight(0);
            mesh.ChannelName(SampleNames.MeshName).SetWeight(0);
        });

        return builder.Build();
    }
}

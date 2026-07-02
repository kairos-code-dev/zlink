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
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("DELIVERYDISPATCH_LOG_DIR"),
            "tracking");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<EvidenceStore>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddRedisLocationStore(redis =>
            {
                redis.ConnectionString = topology.RedisEndpoint;
                redis.KeyPrefix = topology.RedisKeyPrefix;
            });
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("tracking"))
                .TraceLabel("tracking");
            options.AddHandlersFromAssemblyOf(typeof(DeliveryStatusChangedHandler));
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableServer(topology.TrackingRouteEndpoint)
                .SetRoutingId(RoutingId.From("delivery-tracking-server"))
                .AddHandlerGroup(SampleNames.TrackingRouteChannel);
            options.AddClientServerChannel(SampleNames.CustomerRouteChannel)
                .EnableClient()
                .SetRoutingId(RoutingId.From("delivery-tracking-customer-client"));
        });

        return builder.Build();
    }
}

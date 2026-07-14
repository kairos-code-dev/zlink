using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CourierSession;

public static class CourierSessionHostFactory
{
    public static IHost Build(SampleConfiguration configuration)
    {
        var topology = configuration.Topology;
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            configuration.Role.LogDir,
            "courier-session");
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CourierSessionBinder>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(configuration.FlowLogPath)
                .TraceLabel("courier-session");
            options.AddHandlersFromAssemblyOf(typeof(CourierSessionHostFactory));
            options.AddSpotMesh(SampleNames.CourierActorDiscovery)
                .EnableRouter(topology.CourierSessionSpotRouterEndpoint)
                .SetRoutingId(topology.CourierSessionSpotNodeRid)
                .EnablePubSub(topology.CourierSessionSpotEndpoint);
            options.AddStreamNode(SampleNames.CourierStreamNode)
                .Bind(topology.CourierStreamEndpoint)
                .RegisterSession<CourierSession>();
        });

        return builder.Build();
    }
}

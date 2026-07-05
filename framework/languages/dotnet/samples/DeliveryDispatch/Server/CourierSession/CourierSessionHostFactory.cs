using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CourierSession;

public static class CourierSessionHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("DELIVERYDISPATCH_LOG_DIR"),
            "courier-session");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CourierSessionBinder>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("courier-session"))
                .TraceLabel("courier-session");
            options.AddHandlersFromAssemblyOf(typeof(CourierSessionHostFactory));
            options.AddSpotMesh(SampleNames.CourierActorDiscovery)
                .EnableRouter(topology.CourierSessionSpotRouterEndpoint)
                .SetRoutingId(topology.CourierSessionSpotNodeRid)
                .ConnectRouter(topology.CourierActorNode1Rid, topology.CourierActorNode1RouterEndpoint)
                .ConnectRouter(topology.CourierActorNode2Rid, topology.CourierActorNode2RouterEndpoint)
                .EnablePubSub(topology.CourierSessionSpotEndpoint);
            options.AddStreamNode(SampleNames.CourierStreamNode)
                .Bind(topology.CourierStreamEndpoint)
                .RegisterSession<CourierSession>();
        });

        return builder.Build();
    }
}

using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CourierGateway;

public static class CourierGatewayHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("DELIVERYDISPATCH_LOG_DIR"),
            "courier-gateway");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CourierDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("courier-gateway"))
                .TraceLabel("courier-gateway");
            options.AddHandlersFromAssemblyOf(typeof(CourierGatewayHostFactory));
            options.AddClientServerChannel(SampleNames.CourierRouteChannel)
                .EnableServer(topology.CourierRouteEndpoint)
                .SetRoutingId(RoutingId.From("delivery-courier-gateway-server"))
                .AddHandlerGroup(SampleNames.CourierRouteChannel);
            options.AddRouteMesh(SampleNames.CourierActorNodeRouteChannel)
                .EnableClient()
                .SetRoutingId(RoutingId.From("delivery-courier-gateway"));
        });

        return builder.Build();
    }
}

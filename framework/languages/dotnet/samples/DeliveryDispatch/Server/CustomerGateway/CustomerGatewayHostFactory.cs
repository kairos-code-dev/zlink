using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CustomerGateway;

public static class CustomerGatewayHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("DELIVERYDISPATCH_LOG_DIR"),
            "customer-gateway");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CustomerActorDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddRedisLocationStore(redis =>
            {
                redis.ConnectionString = topology.RedisEndpoint;
                redis.KeyPrefix = topology.RedisKeyPrefix;
            });
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("customer-gateway"))
                .TraceLabel("customer-gateway");
            options.AddHandlersFromAssemblyOf(typeof(CustomerGatewayHostFactory));
            options.AddClientServerChannel(SampleNames.CustomerRouteChannel)
                .EnableServer(topology.CustomerRouteEndpoint)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-customer-gateway-server"))
                .AddHandlerGroup(SampleNames.CustomerRouteChannel);
            options.AddSpotMesh(SampleNames.CustomerActorDiscovery)
                .EnableRouter(topology.CustomerSpotRouterEndpoint)
                .SetRoutingId(topology.CustomerSpotNodeRid)
                .EnablePubSub(topology.CustomerSpotEndpoint)
                .AddEntrySpot<CustomerEntrySpot>()
                .AddActorFactory<CustomerActorFactory>(SampleNames.CustomerActorType);
            options.AddStreamNode(SampleNames.CustomerStreamNode)
                .Bind(topology.CustomerStreamEndpoint)
                .RegisterSession<CustomerSession>();
        });

        return builder.Build();
    }
}

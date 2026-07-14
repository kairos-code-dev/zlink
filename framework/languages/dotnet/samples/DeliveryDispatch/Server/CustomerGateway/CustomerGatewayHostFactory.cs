using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CustomerGateway;

public static class CustomerGatewayHostFactory
{
    public static IHost Build(SampleConfiguration configuration)
    {
        var topology = configuration.Topology;
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            configuration.Role.LogDir,
            "customer-gateway");
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CustomerActorDirectory>();
        builder.Services.AddSingleton<CustomerActorAccess>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(configuration.FlowLogPath)
                .TraceLabel("customer-gateway");
            options.AddHandlersFromAssemblyOf(typeof(CustomerGatewayHostFactory));
            options.AddSpotMesh(SampleNames.CustomerActorDiscovery)
                .EnableRouter(topology.CustomerSpotRouterEndpoint)
                .SetRoutingId(topology.CustomerSpotNodeRid)
                .SetEntrySpotRoutingId(topology.CustomerSpotNodeRid)
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

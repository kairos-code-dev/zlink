using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;

namespace DeliveryDispatch.Server.CustomerGateway;

public static class CustomerGatewayHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CustomerActorDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("customer-gateway"))
                .TraceLabel("customer-gateway");
            options.AddHandlersFromAssemblyOf(typeof(CustomerGatewayHostFactory));
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.CustomerRouteChannel)
                .EnableServer(topology.CustomerRouteEndpoint)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-customer-gateway-server"))
                .AddHandlerGroup(SampleNames.CustomerRouteChannel);
            var mesh = options.AddSpotMesh(SampleNames.CustomerActorDiscovery);
            mesh.EnableRouter(topology.CustomerSpotRouterEndpoint)
                .SetRoutingId(topology.CustomerSpotNodeRid);
            mesh.EnablePubSub(topology.CustomerSpotEndpoint);
            mesh.AddEntrySpot<CustomerEntrySpot>();
            mesh.AddActorFactory<CustomerActorFactory>(SampleNames.CustomerActorType);
            options.AddStreamNode(SampleNames.CustomerStreamNode)
                .Bind(topology.CustomerStreamEndpoint)
                .RegisterSession<CustomerSession>();
        });

        return builder.Build();
    }
}

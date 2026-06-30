using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace DeliveryDispatch.Server.CourierGateway;

public static class CourierGatewayHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CourierDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("courier-gateway"))
                .TraceLabel("courier-gateway");
            options.AddHandlersFromAssemblyOf(typeof(CourierGatewayHostFactory));
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.CourierRouteChannel)
                .EnableServer(topology.CourierRouteEndpoint)
                .SetRoutingId(RoutingId.From("delivery-courier-gateway-server"))
                .AddHandlerGroup(SampleNames.CourierRouteChannel);
            options.AddRouteMesh(SampleNames.CourierSpotRouteChannel)
                .EnableClient()
                .SetRoutingId(RoutingId.From("delivery-courier-gateway"));
        });

        return builder.Build();
    }
}

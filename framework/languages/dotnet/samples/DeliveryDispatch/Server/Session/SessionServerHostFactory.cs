using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;

namespace DeliveryDispatch.Server.Session;

public static class SessionServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CustomerSessionDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("session"))
                .TraceLabel("session");
            options.AddHandlersFromAssemblyOf(typeof(CustomerSession));
            options.Codecs.AddJson();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableClient();
            options.AddFanoutChannel(SampleNames.StatusFanoutChannel)
                .EnableSubscriber()
                .AddPublishHandler<DeliveryStatusFanoutHandler, DeliveryStatusNotify>();
            var mesh = options.AddSpotMesh(SampleNames.DeliverySpotDiscovery);
            mesh.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            mesh.EnableRouter(topology.SessionSpotRouterEndpoint)
                .SetRoutingId(topology.SessionSpotNodeRid);
            mesh.EnablePubSub(topology.SessionSpotEndpoint);
            options.AddStreamNode(SampleNames.CustomerStreamNode)
                .Bind(topology.SessionStreamEndpoint)
                .RegisterSession<CustomerSession>();
        });

        return builder.Build();
    }
}

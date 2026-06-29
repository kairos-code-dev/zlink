using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;

namespace DeliveryDispatch.Server.CourierSession;

public static class CourierSessionHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("courier-session"))
                .TraceLabel("courier-session");
            options.AddHandlersFromAssemblyOf(typeof(CourierSessionHostFactory));
            options.Codecs.AddJson();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.CourierRouteChannel)
                .EnableClient(topology.CourierRouteEndpoint)
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-courier-session-client"));
            var mesh = options.AddSpotMesh(SampleNames.CourierActorDiscovery);
            mesh.EnableRouter(topology.CourierSessionSpotRouterEndpoint)
                .SetRoutingId(topology.CourierSessionSpotNodeRid);
            mesh.EnablePubSub(topology.CourierSessionSpotEndpoint);
            mesh.ConnectRouter(topology.CourierSpotNode1Rid, topology.CourierSpotNode1RouterEndpoint);
            mesh.ConnectRouter(topology.CourierSpotNode2Rid, topology.CourierSpotNode2RouterEndpoint);
            mesh.ConnectPeerPub(topology.CourierSpotNode1Endpoint);
            mesh.ConnectPeerPub(topology.CourierSpotNode2Endpoint);
            options.AddStreamNode(SampleNames.CourierStreamNode)
                .Bind(topology.CourierStreamEndpoint)
                .RegisterSession<CourierSession>();
        });

        return builder.Build();
    }
}

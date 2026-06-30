using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CourierActorNode;

public static class NodeHostFactory
{
    public static IHost Build(SampleTopology topology, string node)
    {
        var builder = Host.CreateApplicationBuilder();
        var nodeConfig = ResolveNode(topology, node);
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("DELIVERYDISPATCH_LOG_DIR"),
            $"courier-actor-{nodeConfig.Name}");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<ActorDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path($"courier-actor-{nodeConfig.Name}"))
                .TraceLabel($"courier-actor-{nodeConfig.Name}");
            options.AddHandlersFromAssemblyOf(typeof(NodeHostFactory));
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddRouteMesh(SampleNames.CourierActorNodeRouteChannel)
                .SetRoutingId(nodeConfig.Rid)
                .AddHandlerGroup(SampleNames.CourierActorNodeRouteChannel)
                .EnableServer(nodeConfig.RouteEndpoint)
                .EnableClient();

            options.AddSpotMesh(SampleNames.CourierActorDiscovery)
                .UseRegistrySpotResolver()
                .EnableRouter(nodeConfig.SpotRouterEndpoint)
                .SetRoutingId(nodeConfig.Rid)
                .SetEntrySpotRoutingId(nodeConfig.EntrySpotRid)
                .EnablePubSub(nodeConfig.SpotEndpoint)
                .AddEntrySpot<CourierEntrySpot>()
                .AddActorFactory<CourierActorFactory>(SampleNames.CourierActorType);
        });

        return builder.Build();
    }

    private static NodeOptions ResolveNode(SampleTopology topology, string node)
    {
        return node switch
        {
            "node1" => new NodeOptions(
                "node1",
                topology.CourierActorNode1Rid,
                topology.CourierEntrySpotNode1Rid,
                topology.CourierActorNode1RouteEndpoint,
                topology.CourierActorNode1RouterEndpoint,
                topology.CourierActorNode1Endpoint),
            "node2" => new NodeOptions(
                "node2",
                topology.CourierActorNode2Rid,
                topology.CourierEntrySpotNode2Rid,
                topology.CourierActorNode2RouteEndpoint,
                topology.CourierActorNode2RouterEndpoint,
                topology.CourierActorNode2Endpoint),
            _ => throw new InvalidOperationException($"Unknown courier actor node '{node}'.")
        };
    }

    private sealed record NodeOptions(
        string Name,
        Systems.Zlink.RoutingId Rid,
        Systems.Zlink.RoutingId EntrySpotRid,
        string RouteEndpoint,
        string SpotRouterEndpoint,
        string SpotEndpoint);
}

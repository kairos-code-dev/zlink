using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CourierSpot;

public static class CourierSpotHostFactory
{
    public static IHost Build(SampleTopology topology, string node)
    {
        var builder = Host.CreateApplicationBuilder();
        var nodeConfig = ResolveNode(topology, node);
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("DELIVERYDISPATCH_LOG_DIR"),
            $"courier-spot-{nodeConfig.Name}");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CourierActorDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path($"courier-spot-{nodeConfig.Name}"))
                .TraceLabel($"courier-spot-{nodeConfig.Name}");
            options.AddHandlersFromAssemblyOf(typeof(CourierSpotHostFactory));
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            var route = options.AddRouteMesh(SampleNames.CourierSpotRouteChannel)
                .SetRoutingId(nodeConfig.Rid)
                .AddHandlerGroup(SampleNames.CourierSpotRouteChannel);
            route.EnableServer(nodeConfig.RouteEndpoint);
            route.EnableClient();

            var mesh = options.AddSpotMesh(SampleNames.CourierActorDiscovery)
                .UseRegistrySpotResolver();
            mesh.EnableRouter(nodeConfig.SpotRouterEndpoint)
                .SetRoutingId(nodeConfig.Rid);
            mesh.ConfigureEntrySpot().RoutingId = nodeConfig.EntrySpotRid;
            mesh.EnablePubSub(nodeConfig.SpotEndpoint);
            mesh.AddEntrySpot<CourierEntrySpot>();
            mesh.AddActorFactory<CourierActorFactory>(SampleNames.CourierActorType);
        });

        return builder.Build();
    }

    private static CourierSpotNodeOptions ResolveNode(SampleTopology topology, string node)
    {
        return node switch
        {
            "node1" => new CourierSpotNodeOptions(
                "node1",
                topology.CourierSpotNode1Rid,
                topology.CourierEntrySpotNode1Rid,
                topology.CourierSpotNode1RouteEndpoint,
                topology.CourierSpotNode1RouterEndpoint,
                topology.CourierSpotNode1Endpoint),
            "node2" => new CourierSpotNodeOptions(
                "node2",
                topology.CourierSpotNode2Rid,
                topology.CourierEntrySpotNode2Rid,
                topology.CourierSpotNode2RouteEndpoint,
                topology.CourierSpotNode2RouterEndpoint,
                topology.CourierSpotNode2Endpoint),
            _ => throw new InvalidOperationException($"Unknown courier spot node '{node}'.")
        };
    }

    private sealed record CourierSpotNodeOptions(
        string Name,
        Systems.Zlink.RoutingId Rid,
        Systems.Zlink.RoutingId EntrySpotRid,
        string RouteEndpoint,
        string SpotRouterEndpoint,
        string SpotEndpoint);
}

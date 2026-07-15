using Microsoft.Extensions.Configuration;

using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CourierActorNode;

public static class NodeHostFactory
{
    public static IHost Build(SampleConfiguration configuration)
    {
        var topology = configuration.Topology;
        var node = ResolveNode(configuration);
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            configuration.Role.LogDir,
            configuration.Role.Name);
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<ActorDirectory>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(configuration.FlowLogPath)
                .TraceLabel(configuration.Role.Name);
            options.AddHandlersFromAssemblyOf(typeof(NodeHostFactory));
            options.AddSpotMesh(SampleNames.CourierActorDiscovery)
                .EnableRouter(node.SpotRouterEndpoint)
                .SetRoutingId(node.Rid)
                .SetEntrySpotRoutingId(node.Rid)
                .EnablePubSub(node.SpotEndpoint)
                .AddEntrySpot<CourierEntrySpot>()
                .AddActorFactory<CourierActorFactory>(SampleNames.CourierActorType);
            // The courier's decision goes back to dispatch as its own one-way message, so this
            // node needs a way to speak to the dispatch channel (common sample spec §7.4).
            options.AddClientServerChannel(SampleNames.DispatchChannel)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From($"{configuration.Role.Name}-dispatch"));
        });

        return builder.Build();
    }

    /// <summary>
    /// Which of the two courier nodes this process is. It comes from the role's configuration file,
    /// not from a switch on the command line: the executable has one role, and the file says which
    /// instance of it this is
    /// (framework/doc/framework/common/sample-e2e-configuration-policy.ko.md §2.1).
    /// </summary>
    private static NodeOptions ResolveNode(SampleConfiguration configuration)
    {
        var topology = configuration.Topology;
        var nodeRid = configuration.Role.NodeRid
                      ?? throw new InvalidOperationException(
                          "sample.role.nodeRid is required for a courier actor node.");

        if (nodeRid == topology.CourierActorNode1Rid.ToString())
        {
            return new NodeOptions(
                topology.CourierActorNode1Rid,
                topology.CourierActorNode1RouterEndpoint,
                topology.CourierActorNode1Endpoint);
        }

        if (nodeRid == topology.CourierActorNode2Rid.ToString())
        {
            return new NodeOptions(
                topology.CourierActorNode2Rid,
                topology.CourierActorNode2RouterEndpoint,
                topology.CourierActorNode2Endpoint);
        }

        throw new InvalidOperationException($"Unknown courier actor node '{nodeRid}'.");
    }

    private sealed record NodeOptions(
        Systems.Zlink.RoutingId Rid,
        string SpotRouterEndpoint,
        string SpotEndpoint);
}

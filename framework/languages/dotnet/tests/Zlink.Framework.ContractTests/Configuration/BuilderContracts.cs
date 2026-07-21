using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Configuration;

public sealed class BuilderContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkFrameworkOptions),
        typeof(IZLinkMeshNodeBuilder),
        typeof(IZLinkMeshChannelBuilder),
        typeof(IZLinkFanoutChannelBuilder),
        typeof(IZLinkStreamNodeBuilder),
        typeof(IZLinkStreamCompressionBuilder),
        typeof(IZLinkMeshPeerConnections),
        typeof(IZLinkMeshNodeSocketConfig),
        typeof(IZLinkRouteMeshRuntimeOptions),
        typeof(IZLinkMeshNodeRuntimeOptions),
        typeof(IZLinkMeshChannelRuntimeOptions),
        typeof(IZLinkRouteMeshRuntime),
        typeof(IZLinkMetadataPolicyBuilder),
        typeof(IZLinkEndpointConnections),
        typeof(IZLinkDrainControl),
        typeof(IZLinkSocketConfig),
        typeof(IZLinkRouteConfig),
        typeof(IZLinkOutboundRouteConfig),
        typeof(IZLinkSpotPublisherConfig),
        typeof(IZLinkSpotSubscriberConfig),
        typeof(IZLinkEntrySpotOptions))]
    public void Framework_options_expose_the_10_0_registration_surface()
    {
        var methods = typeof(IZLinkFrameworkOptions)
            .GetMethods()
            .Select(static method => method.Name)
            .ToHashSet(StringComparer.Ordinal);

        Assert.Contains(nameof(IZLinkFrameworkOptions.AddRouteMesh), methods);
        Assert.Contains(nameof(IZLinkFrameworkOptions.AddFanoutChannel), methods);
        Assert.Contains(nameof(IZLinkFrameworkOptions.AddStreamNode), methods);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkMeshNodeBuilder),
        typeof(IZLinkMeshNodeSocketConfig),
        typeof(IZLinkMeshPeerConnections))]
    public void Mesh_node_builder_owns_transport_identity_and_direct_route_handlers()
    {
        var methods = typeof(IZLinkMeshNodeBuilder)
            .GetMethods()
            .Select(static method => method.Name)
            .ToHashSet(StringComparer.Ordinal);

        Assert.Contains(nameof(IZLinkMeshNodeBuilder.Listen), methods);
        Assert.Contains(nameof(IZLinkMeshNodeBuilder.SetRoutingId), methods);
        Assert.Contains(nameof(IZLinkMeshNodeBuilder.UseAllocatedRoutingId), methods);
        Assert.Contains(nameof(IZLinkMeshNodeBuilder.ChannelName), methods);
        Assert.Contains(nameof(IZLinkMeshNodeBuilder.AddRouteSendHandler), methods);
        Assert.Contains(nameof(IZLinkMeshNodeBuilder.AddRouteRequestHandler), methods);
        Assert.NotNull(typeof(IZLinkMeshNodeBuilder).GetProperty(nameof(IZLinkMeshNodeBuilder.PeerConnections)));
    }

    [Fact]
    [ContractExample(typeof(IZLinkMeshChannelBuilder))]
    public void Mesh_channel_builder_owns_weight_and_typed_channel_handlers()
    {
        var methods = typeof(IZLinkMeshChannelBuilder)
            .GetMethods()
            .Select(static method => method.Name)
            .ToHashSet(StringComparer.Ordinal);

        Assert.Contains(nameof(IZLinkMeshChannelBuilder.SetWeight), methods);
        Assert.Contains(nameof(IZLinkMeshChannelBuilder.AddHandlerGroup), methods);
        Assert.Contains(nameof(IZLinkMeshChannelBuilder.AddSendHandler), methods);
        Assert.Contains(nameof(IZLinkMeshChannelBuilder.AddRequestHandler), methods);
        Assert.DoesNotContain("Listen", methods);
        Assert.DoesNotContain("SetRoutingId", methods);
    }

}

using Zlink.Framework.Runtime.Backend.DotNet.Wrappers;
using Zlink.Framework.Runtime.Configuration;

namespace Zlink.Framework.UnitTests;

// S8-07: ConfigureSpotPublisher().NoDrop must reach the native spot publish
// plane (dotnet exact interface 05-route-mesh §5). Core defaults the plane to
// NoDrop = true; an explicit false is pushed through ISpot.SetNoDrop when the
// node creates a spot.
public sealed class SpotPublisherNoDropTests
{
    [Fact]
    public async Task Default_Config_Leaves_The_Core_NoDrop_Default_True()
    {
        var factory = new ZLinkDotNetBackendAdapterFactory();
        var channelAdapter = factory.CreateChannelAdapter();
        await using var context = channelAdapter.CreateContext();
        await using var spotNode = factory.CreateSpotAdapter()
            .CreateSpotNode(context, "nodrop-default-mesh");

        var wrapper = Assert.IsType<ZLinkBackendSpotNodeWrapper>(spotNode);
        spotNode.SetRoutingId(RoutingId.From("nodrop-default-node"));
        spotNode.SetRouterBind("inproc://nodrop-default-node");
        spotNode.AddChannel("nodrop-default-mesh");
        wrapper.ApplyRoleConfig(new ZLinkSpotPublisherConfig(), null);

        await using var spot = wrapper.CreateSpot();
        var nativeSpot = Assert.IsType<ZLinkBackendSpotWrapper>(spot).NativeSpot;

        Assert.True(nativeSpot.GetNoDrop());
    }

    [Fact]
    public async Task Explicit_False_Reaches_The_Spot_Publish_Plane()
    {
        var factory = new ZLinkDotNetBackendAdapterFactory();
        var channelAdapter = factory.CreateChannelAdapter();
        await using var context = channelAdapter.CreateContext();
        await using var spotNode = factory.CreateSpotAdapter()
            .CreateSpotNode(context, "nodrop-false-mesh");

        var wrapper = Assert.IsType<ZLinkBackendSpotNodeWrapper>(spotNode);
        spotNode.SetRoutingId(RoutingId.From("nodrop-false-node"));
        spotNode.SetRouterBind("inproc://nodrop-false-node");
        spotNode.AddChannel("nodrop-false-mesh");
        wrapper.ApplyRoleConfig(new ZLinkSpotPublisherConfig { NoDrop = false }, null);

        await using var spot = wrapper.CreateSpot();
        var nativeSpot = Assert.IsType<ZLinkBackendSpotWrapper>(spot).NativeSpot;
        Assert.False(nativeSpot.GetNoDrop());

        var entrySpot = wrapper.EntrySpot();
        var nativeEntrySpot =
            Assert.IsType<ZLinkBackendSpotWrapper>(entrySpot).NativeSpot;
        Assert.False(nativeEntrySpot.GetNoDrop());
    }
}

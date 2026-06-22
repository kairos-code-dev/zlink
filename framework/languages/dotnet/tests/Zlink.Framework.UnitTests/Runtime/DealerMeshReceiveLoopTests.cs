using Zlink.Framework.Contracts.Registry;
using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Configuration;

namespace Zlink.Framework.UnitTests;

public sealed class DealerMeshReceiveLoopTests
{
    [Fact]
    public void ShouldStartDealerMeshReceiveLoop_ReturnsFalse_ForClientOnlyPeer()
    {
        var channel = new ZLinkChannelRegistration
        {
            ChannelName = "profile.mesh",
            AutoConnectType = ZLinkAutoConnectType.DealerMesh,
            Client = new ZLinkChannelClientCapabilityRegistration(),
        };

        Assert.False(ZLinkChannelRuntimeManager.ShouldStartDealerMeshReceiveLoop(channel));
    }

    [Fact]
    public void ShouldStartDealerMeshReceiveLoop_ReturnsTrue_WhenHandlersAreExposed()
    {
        var channel = new ZLinkChannelRegistration
        {
            ChannelName = "profile.mesh",
            AutoConnectType = ZLinkAutoConnectType.DealerMesh,
            Client = new ZLinkChannelClientCapabilityRegistration(),
        };
        channel.RequestHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(DealerMeshReceiveLoopTests),
            typeof(string),
            typeof(string),
            "ProfileRequest"));

        Assert.True(ZLinkChannelRuntimeManager.ShouldStartDealerMeshReceiveLoop(channel));
    }

    [Fact]
    public void ShouldStartDealerMeshReceiveLoop_ReturnsTrue_WhenHandlerGroupIsMapped()
    {
        var channel = new ZLinkChannelRegistration
        {
            ChannelName = "profile.mesh",
            AutoConnectType = ZLinkAutoConnectType.DealerMesh,
            Client = new ZLinkChannelClientCapabilityRegistration(),
        };
        channel.HandlerGroups.Add("profile-handlers");

        Assert.True(ZLinkChannelRuntimeManager.ShouldStartDealerMeshReceiveLoop(channel));
    }
}

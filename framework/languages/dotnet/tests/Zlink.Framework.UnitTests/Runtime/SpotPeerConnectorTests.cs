using System.Reflection;
using Systems.Zlink;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class SpotPeerConnectorTests
{
    [Fact]
    public void Auto_Router_Connect_Retries_After_Busy()
    {
        var node = DispatchProxy.Create<IZLinkBackendSpotNode, BusyOnceSpotNode>();
        var proxy = (BusyOnceSpotNode)(object)node;
        var connector = new ZLinkSpotPeerConnector(node, new ZLinkSpotPeerConnectionSet());

        Assert.False(connector.ConnectPeerAuto(RoutingId.From("peer"), "tcp://peer:1"));
        Assert.True(connector.ConnectPeerAuto(RoutingId.From("peer"), "tcp://peer:1"));
        Assert.Equal(2, proxy.ConnectAttempts);
    }

    private class BusyOnceSpotNode : DispatchProxy
    {
        internal int ConnectAttempts { get; private set; }

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            ArgumentNullException.ThrowIfNull(targetMethod);
            if (targetMethod.Name != nameof(IZLinkBackendSpotNode.ConnectPeer))
                throw new NotSupportedException(targetMethod.Name);

            ConnectAttempts++;
            if (ConnectAttempts == 1)
                throw new ZlinkConnectException(ZlinkConnectException.ErrorCode.Busy);

            return null;
        }
    }
}

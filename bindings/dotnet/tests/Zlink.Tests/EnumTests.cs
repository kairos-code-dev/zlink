using Xunit;

namespace Zlink.Tests;

public class EnumTests
{
    [Fact]
    public void SocketType_Values_Match_C_Defines()
    {
        Assert.Equal(0, (int)SocketType.Pair);
        Assert.Equal(1, (int)SocketType.Pub);
        Assert.Equal(2, (int)SocketType.Sub);
        Assert.Equal(5, (int)SocketType.Dealer);
        Assert.Equal(6, (int)SocketType.Router);
        Assert.Equal(9, (int)SocketType.XPub);
        Assert.Equal(10, (int)SocketType.XSub);
        Assert.Equal(11, (int)SocketType.Stream);
    }

    [Fact]
    public void ContextOption_Values_Match_C_Defines()
    {
        Assert.Equal(1, (int)ContextOption.IoThreads);
        Assert.Equal(2, (int)ContextOption.MaxSockets);
        Assert.Equal(5, (int)ContextOption.MaxMsgSz);
        Assert.Equal(9, (int)ContextOption.ThreadNamePrefix);
    }

    [Fact]
    public void SocketOption_Values_Match_C_Defines()
    {
        Assert.Equal(17, (int)SocketOption.Linger);
        Assert.Equal(23, (int)SocketOption.SndHwm);
        Assert.Equal(24, (int)SocketOption.RcvHwm);
        Assert.Equal(6, (int)SocketOption.Subscribe);
        Assert.Equal(95, (int)SocketOption.TlsCert);
        Assert.Equal(102, (int)SocketOption.TlsPassword);
        Assert.Equal(117, (int)SocketOption.ZmpMetadata);
    }

    [Fact]
    public void SendFlags_Values_Match_C_Defines()
    {
        Assert.Equal(0, (int)SendFlags.None);
        Assert.Equal(1, (int)SendFlags.DontWait);
        Assert.Equal(2, (int)SendFlags.SendMore);
    }

    [Fact]
    public void ReceiveFlags_Values_Match_C_Defines()
    {
        Assert.Equal(0, (int)ReceiveFlags.None);
        Assert.Equal(1, (int)ReceiveFlags.DontWait);
    }

    [Fact]
    public void SocketEvent_Values_Match_C_Defines()
    {
        Assert.Equal(0x0001, (int)SocketEvent.Connected);
        Assert.Equal(0x0200, (int)SocketEvent.Disconnected);
        Assert.Equal(0xFFFF, (int)SocketEvent.All);
    }

    [Fact]
    public void DisconnectReason_Values_Match_C_Defines()
    {
        Assert.Equal(0, (int)DisconnectReason.Unknown);
        Assert.Equal(5, (int)DisconnectReason.CtxTerm);
    }

    [Fact]
    public void PollEvents_Values_Match_C_Defines()
    {
        Assert.Equal(1, (int)PollEvents.PollIn);
        Assert.Equal(2, (int)PollEvents.PollOut);
        Assert.Equal(4, (int)PollEvents.PollErr);
        Assert.Equal(8, (int)PollEvents.PollPri);
    }

    [Fact]
    public void ServiceType_Values_Match_C_Defines()
    {
        Assert.Equal(1, (int)DiscoveryServiceType.Gateway);
        Assert.Equal(2, (int)DiscoveryServiceType.Spot);
    }

    [Fact]
    public void GatewayLoadBalancing_Values_Match_C_Defines()
    {
        Assert.Equal(0, (int)GatewayLoadBalancing.RoundRobin);
        Assert.Equal(1, (int)GatewayLoadBalancing.Weighted);
    }

    [Fact]
    public void SocketRole_Values_Match_C_Defines()
    {
        Assert.Equal(1, (int)RegistrySocketRole.Pub);
        Assert.Equal(2, (int)RegistrySocketRole.Router);
        Assert.Equal(3, (int)RegistrySocketRole.PeerSub);
        Assert.Equal(1, (int)DiscoverySocketRole.Sub);
        Assert.Equal(1, (int)GatewaySocketRole.Router);
        Assert.Equal(1, (int)ReceiverSocketRole.Router);
        Assert.Equal(2, (int)ReceiverSocketRole.Dealer);
        Assert.Equal(1, (int)SpotNodeSocketRole.Pub);
        Assert.Equal(2, (int)SpotNodeSocketRole.Sub);
        Assert.Equal(3, (int)SpotNodeSocketRole.Dealer);
        Assert.Equal(1, (int)SpotSocketRole.Pub);
        Assert.Equal(2, (int)SpotSocketRole.Sub);
    }

    [Fact]
    public void Flags_Support_Bitwise_Or()
    {
        var flags = SendFlags.DontWait | SendFlags.SendMore;
        Assert.Equal(3, (int)flags);

        var events = SocketEvent.Connected | SocketEvent.Disconnected;
        Assert.Equal(0x0201, (int)events);

        var poll = PollEvents.PollIn | PollEvents.PollOut;
        Assert.Equal(3, (int)poll);
    }
}

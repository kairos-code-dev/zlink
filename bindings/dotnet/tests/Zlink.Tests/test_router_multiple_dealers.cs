using System.Collections.Generic;
using System.Text;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_router_multiple_dealers
{
    [Theory]
    [InlineData("tcp")]
    [InlineData("ipc")]
    [InlineData("inproc")]
    public void router_multiple_dealers(string transport)
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported(transport))
            return;

        using var ctx = new Context();
        using var router = new Socket(ctx, SocketType.Router);
        using var dealer1 = new Socket(ctx, SocketType.Dealer);
        using var dealer2 = new Socket(ctx, SocketType.Dealer);

        dealer1.SetOption(SocketOption.RoutingId, "D1");
        dealer2.SetOption(SocketOption.RoutingId, "D2");

        string endpoint = CoreTestSupport.NewEndpoint(transport,
            "router-multi-dealer");
        router.Bind(endpoint);
        dealer1.Connect(endpoint);
        dealer2.Connect(endpoint);
        Thread.Sleep(100);

        CoreTestSupport.SendBytesWithRetry(dealer1,
            Encoding.UTF8.GetBytes("from_dealer1"), SendFlags.None, 2000);
        CoreTestSupport.SendBytesWithRetry(dealer2,
            Encoding.UTF8.GetBytes("from_dealer2"), SendFlags.None, 2000);

        var received = new Dictionary<string, string>();
        for (int i = 0; i < 2; i++)
        {
            string id = CoreTestSupport.ReceiveStringWithTimeout(router, 64, 2000);
            string payload = CoreTestSupport.ReceiveStringWithTimeout(router, 128,
                2000);
            received[id] = payload;
        }

        Assert.Equal("from_dealer1", received["D1"]);
        Assert.Equal("from_dealer2", received["D2"]);

        CoreTestSupport.SendBytesWithRetry(router, "D1"u8, SendFlags.SendMore,
            2000);
        CoreTestSupport.SendBytesWithRetry(router, "reply_to_d1"u8,
            SendFlags.None, 2000);

        CoreTestSupport.SendBytesWithRetry(router, "D2"u8, SendFlags.SendMore,
            2000);
        CoreTestSupport.SendBytesWithRetry(router, "reply_to_d2"u8,
            SendFlags.None, 2000);

        Assert.Equal("reply_to_d1",
            CoreTestSupport.ReceiveStringWithTimeout(dealer1, 128, 2000));
        Assert.Equal("reply_to_d2",
            CoreTestSupport.ReceiveStringWithTimeout(dealer2, 128, 2000));
    }
}

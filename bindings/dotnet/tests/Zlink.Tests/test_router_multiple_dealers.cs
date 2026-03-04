using System.Collections.Generic;
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

        dealer1.SetOption(SocketOptions.RoutingId, "D1");
        dealer2.SetOption(SocketOptions.RoutingId, "D2");

        string endpoint = CoreTestSupport.NewEndpoint(transport,
            "router-multi-dealer");
        router.Bind(endpoint);
        dealer1.Connect(endpoint);
        dealer2.Connect(endpoint);
        Thread.Sleep(100);

        CoreTestSupport.SendWithRetry(dealer1, "from_dealer1"u8, SendFlags.None,
            2000);
        CoreTestSupport.SendWithRetry(dealer2, "from_dealer2"u8, SendFlags.None,
            2000);

        var received = new Dictionary<string, string>();
        for (int i = 0; i < 2; i++)
        {
            string id = CoreTestSupport.ReceiveUtf8WithTimeout(router, 2000);
            string payload = CoreTestSupport.ReceiveUtf8WithTimeout(router, 2000);
            received[id] = payload;
        }

        Assert.Equal("from_dealer1", received["D1"]);
        Assert.Equal("from_dealer2", received["D2"]);

        CoreTestSupport.SendWithRetry(router, "D1"u8, SendFlags.SendMore, 2000);
        CoreTestSupport.SendWithRetry(router, "reply_to_d1"u8, SendFlags.None,
            2000);

        CoreTestSupport.SendWithRetry(router, "D2"u8, SendFlags.SendMore, 2000);
        CoreTestSupport.SendWithRetry(router, "reply_to_d2"u8, SendFlags.None,
            2000);

        Assert.Equal("reply_to_d1", CoreTestSupport.ReceiveUtf8WithTimeout(dealer1,
            2000));
        Assert.Equal("reply_to_d2", CoreTestSupport.ReceiveUtf8WithTimeout(dealer2,
            2000));
    }
}

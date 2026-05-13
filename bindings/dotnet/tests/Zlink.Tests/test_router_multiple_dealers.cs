using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using Xunit;

namespace Systems.Zlink.Tests;

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
        using var router = new RouterSocket(ctx);
        using var dealer1 = new DealerSocket(ctx);
        using var dealer2 = new DealerSocket(ctx);

        dealer1.SetRoutingId(CoreTestSupport.RoutingIdUtf8("D1"));
        dealer2.SetRoutingId(CoreTestSupport.RoutingIdUtf8("D2"));

        string endpoint = CoreTestSupport.NewEndpoint(transport,
            "router-multi-dealer");
        router.Bind(endpoint);
        dealer1.Connect(endpoint);
        dealer2.Connect(endpoint);
        Thread.Sleep(300);

        CoreTestSupport.SendWithRetry(dealer1, "from_dealer1"u8, 2000);
        CoreTestSupport.SendWithRetry(dealer2, "from_dealer2"u8, 2000);

        var received = new Dictionary<string, string>();
        for (int i = 0; i < 2; i++)
        {
            (string id, string payload) =
                CoreTestSupport.ReceiveRoutedUtf8WithTimeout(router, 2000);
            received[id] = payload;
        }

        Assert.Equal(2, received.Count);
        Assert.Contains("from_dealer1", received.Values);
        Assert.Contains("from_dealer2", received.Values);

        string dealer1RoutingId = received.First(kvp => kvp.Value == "from_dealer1").Key;
        string dealer2RoutingId = received.First(kvp => kvp.Value == "from_dealer2").Key;

        using Message reply1 = Message.FromString("reply_to_d1");
        router.Send(RoutingId.FromBytes(Encoding.UTF8.GetBytes(dealer1RoutingId)))
            .Message(reply1).Submit();

        using Message reply2 = Message.FromString("reply_to_d2");
        router.Send(RoutingId.FromBytes(Encoding.UTF8.GetBytes(dealer2RoutingId)))
            .Message(reply2).Submit();

        Assert.Equal("reply_to_d1", CoreTestSupport.ReceiveUtf8WithTimeout(dealer1,
            2000));
        Assert.Equal("reply_to_d2", CoreTestSupport.ReceiveUtf8WithTimeout(dealer2,
            2000));
    }

}

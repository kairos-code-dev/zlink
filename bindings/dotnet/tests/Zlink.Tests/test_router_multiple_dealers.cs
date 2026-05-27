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

        using Message reply1 = Message.From("reply_to_d1");
        Assert.True(router.Send(
                RoutingId.From(Encoding.UTF8.GetBytes(dealer1RoutingId)))
            .Message(reply1).Submit());

        using Message reply2 = Message.From("reply_to_d2");
        Assert.True(router.Send(
                RoutingId.From(Encoding.UTF8.GetBytes(dealer2RoutingId)))
            .Message(reply2).Submit());

        Assert.Equal("reply_to_d1", CoreTestSupport.ReceiveUtf8WithTimeout(dealer1,
            2000));
        Assert.Equal("reply_to_d2", CoreTestSupport.ReceiveUtf8WithTimeout(dealer2,
            2000));
    }

    [Fact]
    public void received_send_single_message_replies_to_routed_source()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var router = new RouterSocket(ctx);
        using var dealer = new DealerSocket(ctx);

        dealer.SetRoutingId(CoreTestSupport.RoutingIdUtf8("D1"));
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "received-send-routed");
        router.Bind(endpoint);
        dealer.Connect(endpoint);
        Thread.Sleep(100);

        CoreTestSupport.SendWithRetry(dealer, "ping"u8, 2000);

        using var received = new Received();
        router.Recv(received);
        Assert.Equal("ping", Encoding.UTF8.GetString(
            received.SinglePartOrThrow().AsReadOnlySpan()));

        using Message reply = Message.From("pong");
        Assert.True(received.Send().Message(reply).Submit());

        Assert.Equal("pong", CoreTestSupport.ReceiveUtf8WithTimeout(dealer,
            2000));
    }

    [Fact]
    public void routed_direct_send_and_recv_part_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var router = new RouterSocket(ctx);
        using var dealer = new DealerSocket(ctx);

        RoutingId dealerRid = CoreTestSupport.RoutingIdUtf8("D1");
        dealer.SetRoutingId(dealerRid);
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "routed-direct-part");
        router.Bind(endpoint);
        dealer.Connect(endpoint);
        Thread.Sleep(100);

        using Message outbound = Message.From("ping");
        Assert.True(dealer.Send(outbound));

        using var inbound = new Message();
        Assert.True(router.RecvPart(inbound, out RoutingId? sourceRid,
            out bool hasMore));
        Assert.False(hasMore);
        Assert.True(sourceRid.HasValue);
        RoutingId actualSourceRid = sourceRid.GetValueOrDefault();
        Assert.Equal(dealerRid, actualSourceRid);
        Assert.Equal("ping", Encoding.UTF8.GetString(
            inbound.AsReadOnlySpan()));

        using Message reply = Message.From("pong");
        Assert.True(router.Send(actualSourceRid, reply));

        using var dealerInbound = new Message();
        Assert.True(dealer.RecvPart(dealerInbound, out bool dealerHasMore));
        Assert.False(dealerHasMore);
        Assert.Equal("pong", Encoding.UTF8.GetString(
            dealerInbound.AsReadOnlySpan()));
    }

}

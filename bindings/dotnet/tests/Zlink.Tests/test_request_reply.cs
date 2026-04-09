using System;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Zlink.Tests;

public sealed class test_request_reply
{
    [Fact]
    public async Task request_dealer_router_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var routerSocket = new RouterSocket(ctx);
        using var dealerSocket = new DealerSocket(ctx);
        using var router = new RequestRouter(routerSocket);
        using var dealer = new RequestDealer(dealerSocket);

        string endpoint = CoreTestSupport.NewEndpoint("inproc", "request-reply");
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        Thread.Sleep(50);

        using var handled = new ManualResetEventSlim(false);
        router.OnRequest((routingId, correlationId, received) =>
        {
            try
            {
                Assert.Equal("ping", received.SinglePartOrThrow().GetString());
                using Message reply = Message.FromString("pong");
                router.Reply(routingId, correlationId, reply);
            }
            finally
            {
                foreach (Message part in received.Parts)
                    part.Dispose();
                handled.Set();
            }
        });

        using Message request = Message.FromString("ping");
        Received reply = await dealer.RequestAsync(request, TimeSpan.FromSeconds(2));
        try
        {
            Assert.Equal("pong", reply.SinglePartOrThrow().GetString());
        }
        finally
        {
            foreach (Message part in reply.Parts)
                part.Dispose();
        }

        Assert.True(handled.Wait(2000));
    }

    [Fact]
    public void request_router_preserves_data_receive_surface()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var routerSocket = new RouterSocket(ctx);
        using var dealerSocket = new DealerSocket(ctx);
        using var router = new RequestRouter(routerSocket);

        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "request-reply-data");
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        Thread.Sleep(50);

        using Message payload = Message.FromString("plain-data");
        dealerSocket.Send(payload);

        Received received = router.Recv();
        try
        {
            Assert.Equal("plain-data", received.SinglePartOrThrow().GetString());
        }
        finally
        {
            foreach (Message part in received.Parts)
                part.Dispose();
        }
    }
}

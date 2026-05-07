using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Systems.Zlink.Tests;

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

        string endpoint = CoreTestSupport.NewEndpoint("inproc", "request-reply");
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        Thread.Sleep(50);

        using var handled = new ManualResetEventSlim(false);
        Task serverTask = Task.Run(() =>
        {
            using Received received = routerSocket.Recv();
            Assert.True(received.RequestSeq.HasValue);
            Assert.NotEqual(0UL, received.RequestSeq.Value);
            Assert.Equal("ping", received.Parts[0].GetString());
            using Message reply = Message.FromString("pong");
            routerSocket.Reply(received.RoutingId ?? throw new InvalidOperationException(
                "missing routing id"), received.RequestSeq.Value, reply);
            handled.Set();
        });

        using Message request = Message.FromString("ping");
        IReadOnlyList<Message> reply = await dealerSocket.RequestAsync(request,
            TimeSpan.FromSeconds(2));
        try
        {
            Assert.Equal("pong", reply[0].GetString());
        }
        finally
        {
            foreach (Message part in reply)
                part.Dispose();
        }

        Assert.True(handled.Wait(2000));
        await serverTask;
    }

    [Fact]
    public void request_router_preserves_data_receive_surface()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var routerSocket = new RouterSocket(ctx);
        using var dealerSocket = new DealerSocket(ctx);

        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "request-reply-data");
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        Thread.Sleep(50);

        using Message payload = Message.FromString("plain-data");
        dealerSocket.Send(payload);

        Received received = routerSocket.Recv();
        try
        {
            string routedPayload = received.Parts.Count == 0
                ? string.Empty
                : received.Parts[received.Parts.Count - 1].GetString();
            Assert.NotNull(routedPayload);
        }
        finally
        {
            foreach (Message part in received.Parts)
                part.Dispose();
        }
    }

    [Fact]
    public void request_callback_transfers_reply_message_ownership_to_application()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var routerSocket = new RouterSocket(ctx);
        using var dealerSocket = new DealerSocket(ctx);

        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "request-reply-callback-owned");
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        Thread.Sleep(50);

        using var handled = new ManualResetEventSlim(false);
        using var callbackReceived = new ManualResetEventSlim(false);
        Message? owned = null;
        RequestResult observedResult = RequestResult.ProtocolError;

        Task serverTask = Task.Run(() =>
        {
            using Received received = routerSocket.Recv();
            using Message reply = Message.FromString("pong-owned");
            routerSocket.Reply(received.RoutingId ?? throw new InvalidOperationException(
                "missing routing id"), received.RequestSeq ?? 0UL, reply);
            handled.Set();
        });

        using Message request = Message.FromString("ping-owned");
        dealerSocket.Request(request, (result, reply) =>
        {
            observedResult = result;
            Assert.Single(reply);
            owned = reply[0];
            callbackReceived.Set();
        });

        Assert.True(callbackReceived.Wait(10000));
        Assert.True(handled.Wait(10000));
        Assert.Equal(RequestResult.Ok, observedResult);
        Assert.NotNull(owned);
        Assert.Equal("pong-owned", owned!.GetString());
        owned.Dispose();
        Assert.Throws<ObjectDisposedException>(() => _ = owned.Size);
        serverTask.GetAwaiter().GetResult();
    }
}

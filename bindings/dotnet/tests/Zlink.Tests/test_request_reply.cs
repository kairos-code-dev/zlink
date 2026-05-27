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
            var received = new Received();
            routerSocket.Recv(received);
            try
            {
                Assert.True(received.RequestSeq.HasValue);
                Assert.NotEqual(0UL, received.RequestSeq.Value);
                Assert.Equal("ping", received.Parts[0].GetString());
                using Message reply = Message.From("pong");
                routerSocket.Reply(
                    received.RoutingId ?? throw new InvalidOperationException(
                        "missing routing id"), received.RequestSeq.Value)
                    .Message(reply).Submit();
                handled.Set();
            }
            finally
            {
                foreach (Message part in received.Parts) part.Dispose();
            }
        });

        using Message request = Message.From("ping");
        IReadOnlyList<Message> reply = await dealerSocket.Request()
            .Message(request)
            .Timeout(TimeSpan.FromSeconds(2))
            .SubmitAsync();
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
    public void dealer_recv_reply_part_routes_same_sequence_to_source_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var server = new DealerSocket(ctx);
        using var clientA = new DealerSocket(ctx);
        using var clientB = new DealerSocket(ctx);

        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "dealer-directed-reply");
        server.Bind(endpoint);
        clientA.Connect(endpoint);
        clientB.Connect(endpoint);
        Thread.Sleep(50);

        using Message requestA = Message.From("from-a");
        using Message requestB = Message.From("from-b");
        Assert.True(clientA.RequestFrame(1, new[] { requestA }));
        Assert.True(clientB.RequestFrame(1, new[] { requestB }));

        ulong tokenA = 0;
        ulong tokenB = 0;
        for (int i = 0; i < 2; i++)
        {
            using Received received = RecvWithRetry(server);
            Assert.Equal(ReceivedMessageType.Request, received.MessageType);
            Assert.True(received.RequestSeq.HasValue);
            Assert.NotEqual(0UL, received.RequestSeq.Value);
            string payload = received.Parts[0].GetString();
            if (payload == "from-a")
                tokenA = received.RequestSeq.Value;
            else if (payload == "from-b")
                tokenB = received.RequestSeq.Value;
            else
                throw new InvalidOperationException(
                    $"Unexpected payload '{payload}'.");
        }

        Assert.NotEqual(0UL, tokenA);
        Assert.NotEqual(0UL, tokenB);
        Assert.NotEqual(tokenA, tokenB);

        using Message replyB = Message.From("reply-b");
        using Message replyA = Message.From("reply-a");
        server.Reply(tokenB, new[] { replyB });
        server.Reply(tokenA, new[] { replyA });

        using Received clientAReply = RecvWithRetry(clientA);
        Assert.Equal(ReceivedMessageType.Reply, clientAReply.MessageType);
        Assert.Equal(1UL, clientAReply.RequestSeq);
        Assert.Equal("reply-a", clientAReply.Parts[0].GetString());

        using Received clientBReply = RecvWithRetry(clientB);
        Assert.Equal(ReceivedMessageType.Reply, clientBReply.MessageType);
        Assert.Equal(1UL, clientBReply.RequestSeq);
        Assert.Equal("reply-b", clientBReply.Parts[0].GetString());
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

        using Message payload = Message.From("plain-data");
        dealerSocket.Send().Message(payload).Submit();

        var received = new Received();
        routerSocket.Recv(received);
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
    public async Task request_callback_transfers_reply_message_ownership_to_application()
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
            var received = new Received();
            routerSocket.Recv(received);
            try
            {
                using Message reply = Message.From("pong-owned");
                routerSocket.Reply(
                    received.RoutingId ?? throw new InvalidOperationException(
                        "missing routing id"), received.RequestSeq ?? 0UL)
                    .Message(reply).Submit();
                handled.Set();
            }
            finally
            {
                foreach (Message part in received.Parts) part.Dispose();
            }
        });

        using Message request = Message.From("ping-owned");
        dealerSocket.Request().Message(request).Submit((result, reply) =>
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
        await serverTask;
    }

    private static Received RecvWithRetry(DealerSocket socket)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(3);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var received = new Received();
            if (socket.Recv(received, RecvFlags.DontWait))
                return received;

            received.Dispose();
            Thread.Sleep(1);
        }

        throw new TimeoutException("Timed out waiting for dealer message.");
    }
}

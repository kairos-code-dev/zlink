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

        using var ctx = Zlink.CreateContext();
        using var routerSocket = ctx.CreateRouterSocket();
        using var dealerSocket = ctx.CreateDealerSocket();

        string endpoint = CoreTestSupport.NewEndpoint("inproc", "request-reply");
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        Thread.Sleep(50);

        using var handled = new ManualResetEventSlim(false);
        Task serverTask = Task.Run(() =>
        {
            var received = Received.Create();
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
            .Async();
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
    public async Task dealer_received_reply_routes_same_sequence_to_source_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var server = ctx.CreateDealerSocket();
        using var clientA = ctx.CreateDealerSocket();
        using var clientB = ctx.CreateDealerSocket();

        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "dealer-directed-reply");
        server.Bind(endpoint);
        clientA.Connect(endpoint);
        clientB.Connect(endpoint);
        Thread.Sleep(50);

        using Message requestA = Message.From("from-a");
        using Message requestB = Message.From("from-b");
        Task<IReadOnlyList<Message>> requestATask = clientA.Request()
            .Message(requestA)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();
        Task<IReadOnlyList<Message>> requestBTask = clientB.Request()
            .Message(requestB)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();

        Received? receivedA = null;
        Received? receivedB = null;
        for (int i = 0; i < 2; i++)
        {
            Received received = RecvWithRetry(server);
            Assert.Equal(ReceivedMessageType.Request, received.MessageType);
            Assert.True(received.RequestSeq.HasValue);
            Assert.NotEqual(0UL, received.RequestSeq.Value);
            string payload = received.Parts[0].GetString();
            if (payload == "from-a")
                receivedA = received;
            else if (payload == "from-b")
                receivedB = received;
            else
            {
                received.Dispose();
                throw new InvalidOperationException(
                    $"Unexpected payload '{payload}'.");
            }
        }

        Assert.NotNull(receivedA);
        Assert.NotNull(receivedB);
        Assert.NotEqual(receivedA!.RequestSeq, receivedB!.RequestSeq);

        using Message replyB = Message.From("reply-b");
        using Message replyA = Message.From("reply-a");
        receivedB.Reply().Message(replyB).Submit();
        receivedA.Reply().Message(replyA).Submit();
        receivedA.Dispose();
        receivedB.Dispose();

        IReadOnlyList<Message> clientAReply = await requestATask;
        Assert.Equal("reply-a", clientAReply[0].GetString());
        Zlink.MultipartClose(clientAReply);

        IReadOnlyList<Message> clientBReply = await requestBTask;
        Assert.Equal("reply-b", clientBReply[0].GetString());
        Zlink.MultipartClose(clientBReply);
    }

    [Fact]
    public void request_router_preserves_data_receive_surface()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var routerSocket = ctx.CreateRouterSocket();
        using var dealerSocket = ctx.CreateDealerSocket();

        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "request-reply-data");
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        Thread.Sleep(50);

        using Message payload = Message.From("plain-data");
        dealerSocket.Send().Message(payload).Submit();

        var received = Received.Create();
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

        using var ctx = Zlink.CreateContext();
        using var routerSocket = ctx.CreateRouterSocket();
        using var dealerSocket = ctx.CreateDealerSocket();

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
            var received = Received.Create();
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

    private static Received RecvWithRetry(IDealerSocket socket)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(3);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var received = Received.Create();
            if (socket.Recv(received, RecvFlags.DontWait))
                return received;

            received.Dispose();
            Thread.Sleep(1);
        }

        throw new TimeoutException("Timed out waiting for dealer message.");
    }
}

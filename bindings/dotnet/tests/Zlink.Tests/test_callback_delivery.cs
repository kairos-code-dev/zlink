using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_callback_delivery
{
    private static TcpClient ConnectRawClient(int port)
    {
        var client = new TcpClient();
        client.NoDelay = true;
        client.ReceiveTimeout = 5000;
        client.SendTimeout = 5000;
        client.Connect(IPAddress.Loopback, port);
        return client;
    }

    private static byte[] ReceiveExact(NetworkStream stream, int size)
    {
        byte[] buffer = new byte[size];
        int read = 0;
        while (read < size)
        {
            int n = stream.Read(buffer, read, size - read);
            if (n <= 0)
                throw new TimeoutException("stream receive timeout");
            read += n;
        }

        return buffer;
    }

    [Fact]
    public void stream_packet_handler_hops_to_registered_context_and_send_semantics_hold()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var stream = ctx.CreateStreamSocket();
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "callback-delivery-stream");
        int port = CoreTestSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);

        using var callbackSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();
        Message? observedPayload = null;
        int callbackThreadId = -1;

        callbackContext.Invoke(() =>
        {
            stream.OnPacket((StreamPacketHandler)((routingId, header, payload) =>
            {
                callbackThreadId = Environment.CurrentManagedThreadId;
                header.Dispose();
                observedPayload = payload;
                try
                {
                    using Message reply = Message.From("pong");
                    stream.Send(routingId).Message(reply).Submit();
                }
                finally
                {
                    callbackSignal.Set();
                }
            }));
        });

        using var client = ConnectRawClient(port);
        CoreTestSupport.SendStreamPacket(client.GetStream(), "ping"u8);

        Assert.True(callbackSignal.Wait(3000));
        Assert.Equal(callbackContext.ThreadId, callbackThreadId);
        Assert.NotNull(observedPayload);
        Assert.Equal("ping",
            Encoding.UTF8.GetString(observedPayload!.AsReadOnlySpan()));

        byte[] reply = ReceiveExact(client.GetStream(), "pong".Length);
        Assert.Equal("pong", Encoding.UTF8.GetString(reply));
        observedPayload.Dispose();
    }

    [Fact]
    public void socket_monitor_event_runs_inline()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var server = ctx.CreatePairSocket();
        using var client = ctx.CreatePairSocket();
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "callback-delivery-monitor");
        server.Bind(endpoint);

        using var callbackSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();
        SocketMonitorEvent? observed = null;
        int callbackThreadId = -1;

        ISocketMonitor monitor = callbackContext.Invoke(() =>
        {
            ISocketMonitor opened = server.MonitorOpen(
                SocketEvent.ConnectionReady | SocketEvent.Disconnected);
            opened.OnEvent(evt =>
            {
                callbackThreadId = Environment.CurrentManagedThreadId;
                observed = evt;
                callbackSignal.Set();
            });
            return opened;
        });

        try
        {
            client.Connect(endpoint);

            Assert.True(callbackSignal.Wait(3000));
            Assert.NotEqual(callbackContext.ThreadId, callbackThreadId);
            Assert.NotNull(observed);
            Assert.Equal(MonitorEventType.ConnectionReady, observed!.Event);
        }
        finally
        {
            monitor.Dispose();
        }
    }

    [Fact]
    public void spot_route_bridge_request_callback_hops_to_registered_context()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var dealer = ctx.CreateDealerSocket();
        using var router = ctx.CreateRouterSocket();
        using var bridge = node.CreateRouteBridge();
        using var callbackSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();

        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "callback-delivery-spot-route-bridge");
        dealer.SetRoutingId(CoreTestSupport.RoutingIdUtf8("bridge-req-dealer"));
        router.Bind(endpoint);
        dealer.Connect(endpoint);
        Thread.Sleep(100);
        bridge.AttachDealerChannel("svc", dealer);

        int callbackThreadId = -1;
        int callbackCount = 0;
        RequestResult observedResult = RequestResult.ProtocolError;
        string observedPayload = string.Empty;
        Exception? callbackError = null;

        callbackContext.Invoke(() =>
        {
            using Message request = Message.From("ping");
            RoutingId targetSpotRid =
                CoreTestSupport.RoutingIdUtf8("callback-target-spot");
            Assert.True(bridge.Request("svc", targetSpotRid,
                new[] { request }, (result, parts) =>
            {
                callbackThreadId = Environment.CurrentManagedThreadId;
                callbackCount++;
                try
                {
                    observedResult = result;
                    Assert.Single(parts);
                    observedPayload = parts[0].GetString();
                    foreach (Message part in parts)
                        part.Dispose();
                }
                catch (Exception ex)
                {
                    callbackError = ex;
                }
                finally
                {
                    callbackSignal.Set();
                }
            }, timeout: TimeSpan.FromSeconds(2)));
        });

        using var received = Received.Create();
        Assert.True(CoreTestSupport.WaitUntil(
            () => router.Recv(received, RecvFlags.DontWait),
            timeoutMs: 5000));
        try
        {
            Assert.True(received.Parts.Count >= 3);
            Assert.Equal("__zlink.routed_spot.egress.request",
                Encoding.UTF8.GetString(received.Parts[0].AsReadOnlySpan()));
            Assert.Equal("ping", received.Parts[2].GetString());
            Assert.True(received.RequestSeq.HasValue);
            using Message reply = Message.From("pong");
            router.Reply(
                received.RoutingId ?? throw new InvalidOperationException(
                    "missing routing id"), received.RequestSeq.Value)
                .Message(reply).Submit();
        }
        finally
        {
            foreach (Message part in received.Parts) part.Dispose();
        }

        Assert.True(callbackSignal.Wait(3000));
        Assert.Null(callbackError);
        Assert.Equal(callbackContext.ThreadId, callbackThreadId);
        Assert.True(callbackCount >= 1);
        Assert.Equal(RequestResult.Ok, observedResult);
        Assert.Equal("pong", observedPayload);
    }

    [Fact]
    public void spot_direct_reply_dispatch_drains_request_to_spot_callback()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var requester = node.CreateSpot();
        using var responder = node.CreateSpot();
        using var callbackSignal = new ManualResetEventSlim(false);

        var nodeRid = CoreTestSupport.RoutingIdUtf8("dotnet-direct-node");
        var requesterRid = CoreTestSupport.RoutingIdUtf8("dotnet-direct-requester");
        var responderRid = CoreTestSupport.RoutingIdUtf8("dotnet-direct-responder");
        node.SetRoutingId(nodeRid);
        requester.SetRoutingId(requesterRid);
        responder.SetRoutingId(responderRid);

        int requesterDispatchCount = 0;
        int callbackCount = 0;
        RequestResult observedResult = RequestResult.ProtocolError;
        string observedPayload = string.Empty;
        Exception? callbackError = null;

        requester.SetDispatchHandler(info =>
        {
            if (info.Event == SpotDispatchEvent.ChannelReplyReadable
                && info.SubjectKind == SpotDispatchSubjectKind.Spot)
            {
                requesterDispatchCount++;
            }
        });

        responder.SetDispatchHandler(info =>
        {
            if (info.Event != SpotDispatchEvent.RoutedReadable)
                return;

            while (true)
            {
                using var received = Received.Create();
                if (!responder.RecvRouted(received, RecvFlags.DontWait))
                    return;
                using Message reply = Message.From("direct-reply");
                received.Reply().Message(reply).Submit();
            }
        });

        using Message request = Message.From("direct-request");
        requester.RequestToSpot(nodeRid, responderRid)
            .Message(request)
            .Timeout(TimeSpan.FromSeconds(2))
            .Submit((result, parts) =>
            {
                callbackCount++;
                try
                {
                    observedResult = result;
                    Message part = Assert.Single(parts);
                    observedPayload = part.GetString();
                    foreach (Message replyPart in parts)
                        replyPart.Dispose();
                }
                catch (Exception ex)
                {
                    callbackError = ex;
                }
                finally
                {
                    callbackSignal.Set();
                }
            });

        Assert.True(callbackSignal.Wait(3000));
        Assert.Equal(1, requesterDispatchCount);
        Assert.Equal(1, callbackCount);
        Assert.Equal(RequestResult.Ok, observedResult);
        Assert.Equal("direct-reply", observedPayload);
        Assert.Null(callbackError);
    }
}

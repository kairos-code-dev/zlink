using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

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

    private static void SendAll(NetworkStream stream, ReadOnlySpan<byte> payload)
    {
        stream.Write(payload);
        stream.Flush();
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

        using var ctx = new Context();
        using var stream = new StreamSocket(ctx);
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
            stream.OnPacket((StreamPacketHandler)((routingId, payload) =>
            {
                callbackThreadId = Environment.CurrentManagedThreadId;
                observedPayload = payload;
                try
                {
                    using Message reply = Message.FromString("pong");
                    stream.Send(routingId, reply);
                }
                finally
                {
                    callbackSignal.Set();
                }
                return 0;
            }));
        });

        using var client = ConnectRawClient(port);
        SendAll(client.GetStream(), "ping"u8);

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

        using var ctx = new Context();
        using var server = new PairSocket(ctx);
        using var client = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "callback-delivery-monitor");
        server.Bind(endpoint);

        using var callbackSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();
        SocketMonitorEvent? observed = null;
        int callbackThreadId = -1;

        SocketMonitor monitor = callbackContext.Invoke(() =>
        {
            SocketMonitor opened = server.MonitorOpen(
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
    public void spot_channel_reply_dispatch_runs_inline_and_callback_hops_to_registered_context()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();
        using var dealer = new DealerSocket(ctx);
        using var router = new RouterSocket(ctx);
        using var dispatchSignal = new ManualResetEventSlim(false);
        using var callbackSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();

        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "callback-delivery-spot-channel");
        router.Bind(endpoint);
        dealer.Connect(endpoint);
        Thread.Sleep(50);
        node.AttachChannelDealerManual("svc", dealer);

        int dispatchThreadId = -1;
        int callbackThreadId = -1;
        int dispatchCount = 0;
        int callbackCount = 0;
        RequestResult observedResult = RequestResult.ProtocolError;
        string observedPayload = string.Empty;
        bool observedChannelReply = false;
        Exception? dispatchError = null;
        Exception? callbackError = null;

        callbackContext.Invoke(() =>
        {
            spot.OnDispatchEvent((currentSpot, info) =>
            {
                dispatchThreadId = Environment.CurrentManagedThreadId;
                if (info.Event == SpotDispatchEvent.ChannelReplyReadable)
                {
                    observedChannelReply = true;
                    dispatchCount++;
                    try
                    {
                        info.DrainChannelReply();
                    }
                    catch (Exception ex)
                    {
                        dispatchError = ex;
                    }
                    finally
                    {
                        dispatchSignal.Set();
                    }
                }
            });

            using Message request = Message.FromString("ping");
            spot.RequestChannel("svc", request, (result, parts) =>
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
            }, timeout: TimeSpan.FromSeconds(2));
        });

        using Received received = router.Recv();
        Assert.Equal("ping", received.Parts[0].GetString());
        Assert.True(received.RequestSeq.HasValue);
        using Message reply = Message.FromString("pong");
        router.Reply(received.RoutingId ?? throw new InvalidOperationException(
            "missing routing id"), received.RequestSeq.Value, reply);

        Assert.True(dispatchSignal.Wait(3000));
        Assert.True(callbackSignal.Wait(3000));
        Assert.Null(dispatchError);
        Assert.Null(callbackError);
        Assert.True(observedChannelReply);
        Assert.NotEqual(callbackContext.ThreadId, dispatchThreadId);
        Assert.Equal(callbackContext.ThreadId, callbackThreadId);
        Assert.Equal(1, dispatchCount);
        Assert.Equal(1, callbackCount);
        Assert.Equal(RequestResult.Ok, observedResult);
        Assert.Equal("pong", observedPayload);
    }
}

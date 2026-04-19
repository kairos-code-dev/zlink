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
    public void socket_monitor_event_hops_to_registered_context()
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
            Assert.Equal(callbackContext.ThreadId, callbackThreadId);
            Assert.NotNull(observed);
            Assert.Equal(MonitorEventType.ConnectionReady, observed!.Event);
        }
        finally
        {
            monitor.Dispose();
        }
    }

    [Fact]
    public void service_monitor_event_hops_to_registered_context()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        registry.SetBroadcastInterval(50);

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "callback-delivery-service-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "callback-delivery-service-router");
        registry.Bind(registryPub, registryRouter);

        using var discovery = new Discovery(ctx, ServiceType.Socket,
            "callback-delivery-service");
        discovery.ConnectRegistry(registryRouter);

        using var callbackSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();
        var provider = new DealerSocket(ctx);
        provider.AttachDiscovery(discovery);
        ServiceMonitorEvent? observed = null;
        int callbackThreadId = -1;

        ServiceMonitor monitor = callbackContext.Invoke(() =>
        {
            ServiceMonitor opened = discovery.MonitorOpen();
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
            string providerEndpoint = CoreTestSupport.NewEndpoint("tcp",
                "callback-delivery-service-provider");
            provider.Bind(providerEndpoint);

            Assert.True(callbackSignal.Wait(5000));
            Assert.Equal(callbackContext.ThreadId, callbackThreadId);
            Assert.NotNull(observed);
            Assert.Equal<ServiceEventType>(ServiceEventType.DiscoveryServiceUp,
                observed!.EventType);
            Assert.Equal("callback-delivery-service", observed.ServiceName);
        }
        finally
        {
            monitor.Dispose();
            Assert.Throws<ZlinkCloseException>(() => provider.Close());
        }
    }
}

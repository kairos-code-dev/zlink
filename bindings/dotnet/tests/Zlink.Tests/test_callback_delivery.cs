using System;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_callback_delivery
{
    [Fact]
    public void recv_callback_hops_to_registered_context_and_send_semantics_hold()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "callback-delivery-recv");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);

        using var callbackSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();
        string? receivedPayload = null;
        int callbackThreadId = -1;

        callbackContext.Invoke(() =>
        {
            receiver.OnReceive((routingId, parts) =>
            {
                callbackThreadId = Environment.CurrentManagedThreadId;
                receivedPayload = parts[0].GetString();
                try
                {
                    using Message reply = Message.FromString("pong");
                    receiver.Send(reply);
                }
                finally
                {
                    foreach (Message part in parts)
                        part.Dispose();
                    callbackSignal.Set();
                }
            });
        });

        using Message request = Message.FromString("ping");
        sender.Send(request);

        Assert.True(callbackSignal.Wait(3000));
        Assert.Equal(callbackContext.ThreadId, callbackThreadId);
        Assert.Equal("ping", receivedPayload);

        Received reply = CoreTestSupport.ReceiveMessageWithTimeout(sender, 3000);
        try
        {
            Assert.Equal("pong", reply.SinglePartOrThrow().GetString());
        }
        finally
        {
            foreach (Message part in reply.Parts)
                part.Dispose();
        }
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

        using var provider = new DealerSocket(ctx);
        provider.AttachDiscovery(discovery);

        using var callbackSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();
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
        }
    }
}

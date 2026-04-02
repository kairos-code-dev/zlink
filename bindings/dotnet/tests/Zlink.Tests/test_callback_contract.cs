using System;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_callback_contract
{
    [Fact]
    public void recv_handler_transfers_message_ownership_to_application()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "callback-owned");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);

        using var receivedSignal = new ManualResetEventSlim(false);
        Message? owned = null;
        receiver.OnReceive((routingId, parts) =>
        {
            Assert.Single(parts);
            owned = parts[0];
            receivedSignal.Set();
        });

        CoreTestSupport.SendWithRetry(sender, "callback-owned"u8, 3000);

        Assert.True(receivedSignal.Wait(3000));
        Assert.NotNull(owned);
        Assert.Equal("callback-owned", CoreTestSupport.Utf8(owned!));
        owned!.Dispose();
        Assert.Throws<ObjectDisposedException>(() => _ = owned.Size);
    }

    [Fact]
    public void recv_handler_exception_reports_unhandled_callback_exception()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var sender = new PairSocket(ctx);
        using var receiver = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "callback-recv");
        sender.Bind(endpoint);
        receiver.Connect(endpoint);

        using var observedSignal = new ManualResetEventSlim(false);
        Exception? observed = null;
        void OnUnhandled(Exception ex)
        {
            observed = ex;
            observedSignal.Set();
        }

        Runtime.UnhandledCallbackException += OnUnhandled;
        try
        {
            receiver.OnReceive((routingId, parts) =>
            {
                throw new InvalidOperationException("recv-handler-fail");
            });

            CoreTestSupport.SendWithRetry(sender, "callback"u8, 3000);

            Assert.True(observedSignal.Wait(3000));
            Assert.NotNull(observed);
            Assert.Equal("recv-handler-fail", observed!.Message);
        }
        finally
        {
            Runtime.UnhandledCallbackException -= OnUnhandled;
        }
    }

    [Fact]
    public void subscribe_handler_transfers_message_ownership_to_application()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var publisher = new PubSocket(ctx);
        using var subscriber = new SubSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "callback-subscribe-owned");
        publisher.Bind(endpoint);
        subscriber.Connect(endpoint);
        subscriber.SetSubscription("callback");

        using var receivedSignal = new ManualResetEventSlim(false);
        Message? owned = null;
        string? receivedTopic = null;
        subscriber.OnSubscribe((routingId, topic, parts) =>
        {
            Assert.Single(parts);
            receivedTopic = topic;
            owned = parts[0];
            receivedSignal.Set();
        });

        CoreTestSupport.PublishWithRetry(publisher, "callback",
            "payload"u8, 3000);

        Assert.True(receivedSignal.Wait(3000));
        Assert.Equal("callback", receivedTopic);
        Assert.NotNull(owned);
        Assert.Equal("payload", CoreTestSupport.Utf8(owned!));
        owned!.Dispose();
        Assert.Throws<ObjectDisposedException>(() => _ = owned.Size);
    }

    [Fact]
    public void subscribe_handler_exception_reports_unhandled_callback_exception()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var publisher = new PubSocket(ctx);
        using var subscriber = new SubSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("inproc",
            "callback-subscribe");
        publisher.Bind(endpoint);
        subscriber.Connect(endpoint);
        subscriber.SetSubscription("callback");

        using var observedSignal = new ManualResetEventSlim(false);
        Exception? observed = null;
        void OnUnhandled(Exception ex)
        {
            observed = ex;
            observedSignal.Set();
        }

        Runtime.UnhandledCallbackException += OnUnhandled;
        try
        {
            subscriber.OnSubscribe((routingId, topic, parts) =>
            {
                throw new InvalidOperationException("subscribe-handler-fail");
            });

            CoreTestSupport.PublishWithRetry(publisher, "callback",
                "payload"u8, 3000);

            Assert.True(observedSignal.Wait(3000));
            Assert.NotNull(observed);
            Assert.Equal("subscribe-handler-fail", observed!.Message);
        }
        finally
        {
            Runtime.UnhandledCallbackException -= OnUnhandled;
        }
    }
}

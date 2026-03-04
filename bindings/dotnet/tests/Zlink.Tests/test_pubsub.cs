using System;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_pubsub
{
    [Theory]
    [InlineData("tcp")]
    [InlineData("ipc")]
    [InlineData("inproc")]
    public void pubsub_basic(string transport)
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported(transport))
            return;

        using var ctx = new Context();
        using var publisher = new Socket(ctx, SocketType.Pub);
        using var subscriber = new Socket(ctx, SocketType.Sub);

        string endpoint = CoreTestSupport.NewEndpoint(transport, "pubsub");
        publisher.Bind(endpoint);
        subscriber.Connect(endpoint);
        subscriber.SetOption(SocketOption.Subscribe, Array.Empty<byte>());
        Thread.Sleep(100);

        CoreTestSupport.SendBytesWithRetry(publisher, "test"u8, SendFlags.None,
            2000);
        Assert.Equal("test",
            CoreTestSupport.ReceiveStringWithTimeout(subscriber, 64, 2000));
    }

    [Theory]
    [InlineData("tcp")]
    [InlineData("ipc")]
    [InlineData("inproc")]
    public void pubsub_filtering_by_topic(string transport)
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported(transport))
            return;

        using var ctx = new Context();
        using var publisher = new Socket(ctx, SocketType.Pub);
        using var subscriber = new Socket(ctx, SocketType.Sub);

        string endpoint = CoreTestSupport.NewEndpoint(transport, "pubsub-filter");
        publisher.Bind(endpoint);
        subscriber.Connect(endpoint);
        subscriber.SetOption(SocketOption.Subscribe, "topicA");
        Thread.Sleep(100);

        CoreTestSupport.SendBytesWithRetry(publisher, "topicA hello"u8,
            SendFlags.None, 2000);
        CoreTestSupport.SendBytesWithRetry(publisher, "topicB world"u8,
            SendFlags.None, 2000);
        CoreTestSupport.SendBytesWithRetry(publisher, "topicA test"u8,
            SendFlags.None, 2000);

        Assert.Equal("topicA hello",
            CoreTestSupport.ReceiveStringWithTimeout(subscriber, 128, 2000));
        Assert.Equal("topicA test",
            CoreTestSupport.ReceiveStringWithTimeout(subscriber, 128, 2000));
        Assert.True(CoreTestSupport.ExpectNoMessage(subscriber, 150));
    }

    [Theory]
    [InlineData("tcp")]
    [InlineData("ipc")]
    [InlineData("inproc")]
    public void xpub_xsub_roundtrip(string transport)
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported(transport))
            return;

        using var ctx = new Context();
        using var xpub = new Socket(ctx, SocketType.XPub);
        using var xsub = new Socket(ctx, SocketType.XSub);

        string endpoint = CoreTestSupport.NewEndpoint(transport, "xpub-xsub");
        xpub.Bind(endpoint);
        xsub.Connect(endpoint);

        CoreTestSupport.SendBytesWithRetry(xsub, new byte[] { 0x01 },
            SendFlags.None, 2000);
        _ = CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000);
        Thread.Sleep(50);

        CoreTestSupport.SendBytesWithRetry(xpub, "xpub_xsub_test"u8,
            SendFlags.None, 2000);
        Assert.Equal("xpub_xsub_test",
            CoreTestSupport.ReceiveStringWithTimeout(xsub, 128, 2000));
    }
}

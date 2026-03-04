using System;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_xpub_verbose
{
    private static readonly byte[] SubscribeA = { 1, (byte)'A' };
    private static readonly byte[] SubscribeB = { 1, (byte)'B' };
    private static readonly byte[] UnsubscribeA = { 0, (byte)'A' };

    [Fact]
    public void xpub_verbose_one_subscriber_duplicate_subscription_visible()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var xpub = new Socket(ctx, SocketType.XPub);
        using var sub = new Socket(ctx, SocketType.Sub);

        string endpoint = CoreTestSupport.NewEndpoint("inproc", "xpub-verbose");
        xpub.Bind(endpoint);
        sub.Connect(endpoint);

        sub.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        sub.SetOption(SocketOption.Subscribe, "B");
        Assert.Equal(SubscribeB,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        sub.SetOption(SocketOption.Subscribe, "A");
        Thread.Sleep(30);
        Assert.True(CoreTestSupport.ExpectNoMessage(xpub, 150));

        xpub.SetOption(SocketOption.XPubVerbose, 1);
        sub.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        CoreTestSupport.SendBytesWithRetry(xpub, "A"u8, SendFlags.None, 2000);
        CoreTestSupport.SendBytesWithRetry(xpub, "B"u8, SendFlags.None, 2000);

        Assert.Equal("A", CoreTestSupport.ReceiveStringWithTimeout(sub, 64, 2000));
        Assert.Equal("B", CoreTestSupport.ReceiveStringWithTimeout(sub, 64, 2000));
    }

    [Fact]
    public void xpub_verbose_two_subscribers_duplicate_subscription_visible()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var xpub = new Socket(ctx, SocketType.XPub);
        using var sub0 = new Socket(ctx, SocketType.Sub);
        using var sub1 = new Socket(ctx, SocketType.Sub);

        string endpoint = CoreTestSupport.NewEndpoint("inproc", "xpub-two");
        xpub.Bind(endpoint);
        sub0.Connect(endpoint);
        sub1.Connect(endpoint);

        sub0.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        sub1.SetOption(SocketOption.Subscribe, "A");
        Assert.True(CoreTestSupport.ExpectNoMessage(xpub, 150));

        sub0.SetOption(SocketOption.Subscribe, "B");
        Assert.Equal(SubscribeB,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        xpub.SetOption(SocketOption.XPubVerbose, 1);
        sub1.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        CoreTestSupport.SendBytesWithRetry(xpub, "A"u8, SendFlags.None, 2000);
        CoreTestSupport.SendBytesWithRetry(xpub, "B"u8, SendFlags.None, 2000);

        Assert.Equal("A", CoreTestSupport.ReceiveStringWithTimeout(sub0, 64, 2000));
        Assert.Equal("A", CoreTestSupport.ReceiveStringWithTimeout(sub1, 64, 2000));
        Assert.Equal("B", CoreTestSupport.ReceiveStringWithTimeout(sub0, 64, 2000));
    }

    [Fact]
    public void xpub_verboser_one_subscriber_unsubscribe_visibility()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var xpub = new Socket(ctx, SocketType.XPub);
        using var sub = new Socket(ctx, SocketType.Sub);

        string endpoint = CoreTestSupport.NewEndpoint("inproc", "xpub-verboser1");
        xpub.Bind(endpoint);
        sub.Connect(endpoint);

        sub.SetOption(SocketOption.Unsubscribe, "A");
        Assert.True(CoreTestSupport.ExpectNoMessage(xpub, 100));

        sub.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        sub.SetOption(SocketOption.Subscribe, "A");
        Assert.True(CoreTestSupport.ExpectNoMessage(xpub, 100));

        sub.SetOption(SocketOption.Unsubscribe, "A");
        sub.SetOption(SocketOption.Unsubscribe, "A");
        Assert.Equal(UnsubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));
        Assert.True(CoreTestSupport.ExpectNoMessage(xpub, 100));

        xpub.SetOption(SocketOption.XPubVerboser, 1);
        sub.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        sub.SetOption(SocketOption.Unsubscribe, "A");
        Assert.Equal(UnsubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));
    }

    [Fact]
    public void xpub_verboser_two_subscribers_unsubscribe_visibility()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var xpub = new Socket(ctx, SocketType.XPub);
        using var sub0 = new Socket(ctx, SocketType.Sub);
        using var sub1 = new Socket(ctx, SocketType.Sub);

        string endpoint = CoreTestSupport.NewEndpoint("inproc", "xpub-verboser2");
        xpub.Bind(endpoint);
        sub0.Connect(endpoint);
        sub1.Connect(endpoint);

        sub0.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        sub1.SetOption(SocketOption.Subscribe, "A");
        Assert.True(CoreTestSupport.ExpectNoMessage(xpub, 150));

        sub0.SetOption(SocketOption.Unsubscribe, "A");
        Assert.True(CoreTestSupport.ExpectNoMessage(xpub, 150));

        sub1.SetOption(SocketOption.Unsubscribe, "A");
        Assert.Equal(UnsubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));
        Assert.True(CoreTestSupport.ExpectNoMessage(xpub, 100));

        xpub.SetOption(SocketOption.XPubVerboser, 1);
        sub0.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));
        sub1.SetOption(SocketOption.Subscribe, "A");
        Assert.Equal(SubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));

        sub1.SetOption(SocketOption.Unsubscribe, "A");
        Assert.Equal(UnsubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));
        sub0.SetOption(SocketOption.Unsubscribe, "A");
        Assert.Equal(UnsubscribeA,
            CoreTestSupport.ReceiveBytesWithTimeout(xpub, 16, 2000));
    }
}

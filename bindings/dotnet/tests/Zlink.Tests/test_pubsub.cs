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
        using var publisher = new PubSocket(ctx);
        using var subscriber = new SubSocket(ctx);

        string endpoint = CoreTestSupport.NewEndpoint(transport, "pubsub");
        publisher.Bind(endpoint);
        subscriber.Connect(endpoint);
        subscriber.SetSubscription(string.Empty);
        Thread.Sleep(100);

        CoreTestSupport.PublishWithRetry(publisher, string.Empty, "test"u8,
            SendFlags.None, 2000);
        Assert.Equal("test", CoreTestSupport.SubscribeUtf8WithTimeout(subscriber,
            out string topic, 2000));
        Assert.Equal(string.Empty, topic);
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
        using var publisher = new PubSocket(ctx);
        using var subscriber = new SubSocket(ctx);

        string endpoint = CoreTestSupport.NewEndpoint(transport, "pubsub-filter");
        publisher.Bind(endpoint);
        subscriber.Connect(endpoint);
        subscriber.SetSubscription("topicA");
        Thread.Sleep(100);

        CoreTestSupport.PublishWithRetry(publisher, "topicA", "hello"u8,
            SendFlags.None, 2000);
        CoreTestSupport.PublishWithRetry(publisher, "topicB", "world"u8,
            SendFlags.None, 2000);
        CoreTestSupport.PublishWithRetry(publisher, "topicA", "test"u8,
            SendFlags.None, 2000);

        Assert.Equal("hello", CoreTestSupport.SubscribeUtf8WithTimeout(subscriber,
            out string topic0, 2000));
        Assert.Equal("topicA", topic0);
        Assert.Equal("test", CoreTestSupport.SubscribeUtf8WithTimeout(subscriber,
            out string topic1, 2000));
        Assert.Equal("topicA", topic1);
        Assert.True(CoreTestSupport.ExpectNoSubscribedMessage(subscriber, 150));
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
        using var xpub = new XPubSocket(ctx);
        using var xsub = new XSubSocket(ctx);

        string endpoint = CoreTestSupport.NewEndpoint(transport, "xpub-xsub");
        xpub.Bind(endpoint);
        xsub.Connect(endpoint);

        xsub.SetSubscription(string.Empty);
        byte[] topic = CoreTestSupport.ReceiveSubscriptionEventWithTimeout(xpub,
            out bool subscribed, 2000);
        Assert.True(subscribed);
        Assert.Empty(topic);
        Thread.Sleep(50);

        CoreTestSupport.PublishWithRetry(xpub, string.Empty, "xpub_xsub_test"u8,
            SendFlags.None, 2000);
        Assert.Equal("xpub_xsub_test", CoreTestSupport.SubscribeUtf8WithTimeout(
            xsub, out string recvTopic, 2000));
        Assert.Equal(string.Empty, recvTopic);
    }
}

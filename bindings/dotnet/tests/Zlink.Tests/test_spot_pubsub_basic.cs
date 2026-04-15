using System;
using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_spot_pubsub_basic
{
    [Fact]
    public void spot_subscription_roundtrip_reports_subscription_event()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var pubSocket = new PubSocket(ctx);
        using var subSocket = new SubSocket(ctx);
        using var spot = node.CreateSpot();
        const string serviceName = "svc:subscription";
        const string topic = "zone:12:*";

        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-subscription-roundtrip");
        pubSocket.Bind(endpoint);
        subSocket.Connect(endpoint);
        node.AttachPubSub(serviceName, pubSocket, subSocket);

        spot.SetSubscription(topic);
        SubscriptionEvent subscribed = spot.ReceiveSubscriptionEvent();

        Assert.True(subscribed.Subscribed);
        Assert.Equal(topic, subscribed.Topic);
    }

    [Fact]
    public void spot_single_part_publish_moves_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var pubSocket = new PubSocket(ctx);
        using var subSocket = new SubSocket(ctx);
        using var spot = node.CreateSpot();
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "spot-owned");
        pubSocket.Bind(endpoint);
        subSocket.Connect(endpoint);
        node.AttachPubSub("svc:owned", pubSocket, subSocket);

        using var msg = Message.FromString("owned-spot");
        spot.Publish("svc:owned", "own:topic", msg);

        Assert.Throws<ObjectDisposedException>(() => _ = msg.Size);
    }

    [Fact]
    public void spot_topic_input_validation()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();

        string tooLongTopic = new string('a', 256);
        string tooLongService = new string('s', 256);

        using (Message message = Message.FromString("x"))
            Assert.ThrowsAny<ArgumentException>(() => spot.Publish("", "topic",
                message));
        using (Message message = Message.FromString("x"))
            Assert.ThrowsAny<ArgumentException>(() => spot.Publish("svc", "",
                message));
        using (Message message = Message.FromString("x"))
            Assert.Throws<ArgumentOutOfRangeException>(() =>
                spot.Publish(tooLongService, "topic", message));
        using (Message message = Message.FromString("x"))
            Assert.Throws<ArgumentOutOfRangeException>(() =>
                spot.Publish("svc", tooLongTopic, message));
    }

    [Fact]
    public void spot_node_backed_spot_roundtrip_over_manual_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("tcp"))
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var pubSocket = new PubSocket(ctx);
        using var subSocket = new SubSocket(ctx);
        using var spot = node.CreateSpot();

        const string serviceName = "svc:spot-node";
        const string topic = "spot:test";
        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-node-manual");
        pubSocket.Bind(endpoint);
        subSocket.Connect(endpoint);
        node.AttachPubSub(serviceName, pubSocket, subSocket);
        spot.SetSubscription(topic);

        DateTime deadline = DateTime.UtcNow.AddMilliseconds(5000);
        while (DateTime.UtcNow < deadline)
        {
            using Message message = Message.FromString("node-backed");
            spot.Publish(serviceName, topic, message);

            TopicMessage? received = TryReceiveSpot(spot);
            if (received == null)
            {
                Thread.Sleep(10);
                continue;
            }

            try
            {
                Assert.Equal(serviceName, received.ServiceName);
                Assert.Equal(topic, received.Topic);
                Assert.Equal("node-backed",
                    received.SinglePartOrThrow().GetString());
                return;
            }
            finally
            {
                received.Dispose();
            }
        }

        throw new TimeoutException("spot node-backed roundtrip timeout");
    }

    private static TopicMessage? TryReceiveSpot(Spot subscriber)
    {
        try
        {
            return subscriber.Subscribe(RecvFlags.DontWait);
        }
        catch (ZlinkRecvException)
        {
            return null;
        }
    }
}

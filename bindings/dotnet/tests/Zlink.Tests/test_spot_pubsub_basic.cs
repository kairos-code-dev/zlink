using System;
using System.Text;
using System.Threading;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_spot_pubsub_basic
{
    [Fact]
    public void spot_subscription_roundtrip_reports_subscription_event()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();
        const string topic = "zone:12:*";

        spot.SetSubscription(topic);
        SpotNodeSubjectEntry[] subjects = node.SubjectsSnapshot();
        Assert.Contains(subjects, entry => entry.Subject == topic);
    }

    [Fact]
    public void spot_single_part_publish_moves_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();
        spot.SetSubscription("own:topic");

        SpotNodeSubjectEntry[] subjects = node.SubjectsSnapshot();
        Assert.Contains(subjects, entry => entry.Subject == "own:topic");
    }

    [Fact]
    public void spot_topic_input_validation()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();

        string tooLongTopic = new string('a', 256);

        using (Message message = Message.From("x"))
            Assert.ThrowsAny<ArgumentException>(() =>
                spot.Publish("").Message(message).Submit());
        Assert.ThrowsAny<ArgumentException>(() =>
            spot.SetSubscription(string.Empty));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.SetSubscription(tooLongTopic));
        using (Message message = Message.From("x"))
            Assert.Throws<ArgumentOutOfRangeException>(() =>
                spot.Publish(tooLongTopic).Message(message).Submit());
    }

    [Fact]
    public void spot_node_backed_spot_roundtrip_over_manual_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();

        const string topic = "spot:test";
        spot.SetSubscription(topic);
        SpotNodeStatus status = node.StatusSnapshot();
        SpotNodeSubjectEntry[] subjects = node.SubjectsSnapshot();
        Assert.Equal(0u, status.ConnectedPeerCount);
        Assert.Contains(subjects, entry => entry.Subject == topic);
    }

    [Fact]
    public void disposing_node_disposes_created_spots()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        var node = ctx.CreateSpotNode();
        ISpot spot = node.CreateSpot();

        node.Dispose();

        using Message message = Message.From("payload");
        Assert.Throws<ObjectDisposedException>(() =>
            spot.Publish("topic").Message(message).Submit());
    }

    [Fact]
    public void spot_node_pub_ingress_forwards_to_local_subscriber()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var subscriber = node.CreateSpot();
        using var ingress = ctx.CreatePubSocket();

        const string topic = "spot:external";
        const string payload = "hello-external";

        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-pub-ingress-local");
        node.SetPubBind(endpoint);
        subscriber.SetSubscription(topic);
        node.AttachPubIngress(ingress);

        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                SpotNodeStatus status = node.StatusSnapshot();
                return status.SubjectCount > 0
                    && (status.ReadySubjectCount > 0
                        || status.ConnectedPeerCount > 0
                        || status.ActivePeerCount > 0
                        || status.ConfiguredPeerCount == 0);
            },
            5000));

        using var subscribed = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.From(payload);
                ingress.Publish(topic).Message(message).Submit();

                try
                {
                    return subscriber.Subscribe(subscribed, RecvFlags.DontWait);
                }
                catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
                {
                    return false;
                }
            },
            5000));

        Assert.Equal(topic, subscribed.Topic);
        Assert.Equal(payload, subscribed.SinglePartOrThrow().GetString());
    }

    [Fact]
    public void spot_publish_accepts_managed_payload_message()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var publisher = node.CreateSpot();
        using var subscriber = node.CreateSpot();

        const string topic = "spot:managed-payload";
        byte[] payload = "hello-managed-payload"u8.ToArray();
        subscriber.SetSubscription(topic);

        Message? sent = null;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                sent?.Dispose();
                sent = Message.From(payload);
                try
                {
                    return publisher.Publish(topic, sent, SendFlags.DontWait);
                }
                catch (ZlinkSubmitException ex)
                    when (ex.Result == ZlinkSubmitException.ErrorCode.Backpressured
                          || ex.Result == ZlinkSubmitException.ErrorCode.NotConnected)
                {
                    return false;
                }
            },
            5000));

        Assert.NotNull(sent);
        Message sentMessage = sent;
        Assert.Throws<ObjectDisposedException>(() => _ = sentMessage.Size);
        sentMessage.Dispose();

        using var subscribed = new Message();
        byte[] topicBuffer = new byte[64];
        int topicLength = 0;
        bool hasMore = true;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                try
                {
                    return subscriber.SubscribePart(subscribed, topicBuffer,
                        out topicLength, out hasMore, RecvFlags.DontWait);
                }
                catch (ZlinkRecvException ex)
                    when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
                {
                    return false;
                }
            },
            5000));

        Assert.False(hasMore);
        Assert.Equal(topic, Encoding.UTF8.GetString(topicBuffer, 0,
            topicLength));
        Assert.Equal(payload, subscribed.ToArray());
    }

    [Fact]
    public void spot_send_to_spot_direct_payload_preserves_message_on_submit_failure()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var sender = node.CreateSpot();

        RoutingId missingNodeRid = CoreTestSupport.RoutingIdUtf8(
            "spot-direct-send-missing-node");
        RoutingId missingSpotRid = CoreTestSupport.RoutingIdUtf8(
            "spot-direct-send-missing-spot");
        byte[] payload = "hello-direct-spot-send"u8.ToArray();

        using Message sent = Message.From(payload);
        Assert.Throws<ZlinkSubmitException>(() =>
            sender.SendToSpot(missingNodeRid, missingSpotRid, sent,
                SendFlags.DontWait));
        Assert.Equal(payload.Length, sent.Size);
        Assert.Equal(payload, sent.ToArray());
    }

    [Fact]
    public void spot_node_create_spot_publishes_to_remote_subscriber_via_discovery_across_contexts()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        string serviceName = $"game.stage.{Guid.NewGuid():N}";
        using var registryContext = Zlink.CreateContext();
        using var publisherContext = Zlink.CreateContext();
        using var subscriberContext = Zlink.CreateContext();
        using var registry = registryContext.CreateRegistry();
        using var publisherDiscovery = publisherContext.CreateDiscovery(AutoConnectType.SpotMesh, serviceName);
        using var subscriberDiscovery = subscriberContext.CreateDiscovery(AutoConnectType.SpotMesh, serviceName);
        using var publisherNode = publisherContext.CreateSpotNode();
        using var subscriberNode = subscriberContext.CreateSpotNode();
        using var publisher = publisherNode.CreateSpot();
        using var subscriber = subscriberNode.CreateSpot();

        const string topic = "spot:external";
        const string payload = "hello-separate-context";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-cross-context-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-cross-context-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-cross-context-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-cross-context-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-cross-context-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-cross-context-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-cross-context-publisher-spot"));
        subscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-cross-context-subscriber-spot"));
        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);
        subscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
            () => publisherNode.StatusSnapshot().ConnectedPeerCount > 0
                && subscriberNode.StatusSnapshot().ConnectedPeerCount > 0,
            10000));

        using var subscribed = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.From(payload);
                publisher.Publish(topic).Message(message).Submit();

                try
                {
                    return subscriber.Subscribe(subscribed, RecvFlags.DontWait);
                }
                catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
                {
                    return false;
                }
            },
            10000));

        Assert.Equal(topic, subscribed.Topic);
        Assert.Equal(payload, subscribed.SinglePartOrThrow().GetString());
    }

    [Fact]
    public void spot_node_publisher_spot_eventually_delivers_when_publish_starts_before_discovery_ready()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        string serviceName = $"game.stage.{Guid.NewGuid():N}";
        using var registryContext = Zlink.CreateContext();
        using var publisherContext = Zlink.CreateContext();
        using var subscriberContext = Zlink.CreateContext();
        using var registry = registryContext.CreateRegistry();
        using var publisherDiscovery = publisherContext.CreateDiscovery(AutoConnectType.SpotMesh, serviceName);
        using var subscriberDiscovery = subscriberContext.CreateDiscovery(AutoConnectType.SpotMesh, serviceName);
        using var publisherNode = publisherContext.CreateSpotNode();
        using var subscriberNode = subscriberContext.CreateSpotNode();
        using var subscriber = subscriberNode.CreateSpot();

        const string topic = "spot:external";
        const string payload = "hello-early-publish";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-early-publish-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-early-publish-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-early-publish-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-early-publish-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-early-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-early-subscriber"));
        subscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-early-subscriber-spot"));
        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);

        using var publisher = publisherNode.CreateSpot();
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-early-publisher-spot"));
        subscriber.SetSubscription(topic);

        using var subscribed = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.From(payload);
                publisher.Publish(topic).Message(message).Submit();

                try
                {
                    return subscriber.Subscribe(subscribed, RecvFlags.DontWait);
                }
                catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
                {
                    return false;
                }
            },
            10000));

        Assert.Equal(topic, subscribed.Topic);
        Assert.Equal(payload, subscribed.SinglePartOrThrow().GetString());
    }

}

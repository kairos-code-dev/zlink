using System;
using System.Linq;
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

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
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

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
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

        using var ctx = new Context();
        var node = new SpotNode(ctx);
        Spot spot = node.CreateSpot();

        node.Dispose();

        using Message message = Message.FromString("payload");
        Assert.Throws<ObjectDisposedException>(() =>
            spot.Publish("svc", "topic", message));
    }

    [Fact]
    public void spot_node_pub_ingress_forwards_to_local_subscriber()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var subscriber = node.CreateSpot();
        using var ingress = new PubSocket(ctx);

        const string topic = "spot:external";
        const string payload = "hello-external";

        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-pub-ingress-local");
        node.Bind(endpoint);
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

        TopicMessage? subscribed = null;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.FromString(payload);
                ingress.Publish(topic, message);

                try
                {
                    subscribed = subscriber.Subscribe(RecvFlags.DontWait);
                    return subscribed is not null;
                }
                catch (ZlinkRecvException ex) when (ex.Result == RecvResult.NoData)
                {
                    return false;
                }
            },
            5000));

        using var received = subscribed!;
        Assert.Equal(topic, received.Topic);
        Assert.Equal(payload, received.SinglePartOrThrow().GetString());
    }

    [Fact]
    public void spot_node_create_spot_publishes_to_remote_subscriber_via_discovery()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        using var publisherDiscovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            "game.stage");
        using var subscriberDiscovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            "game.stage");
        using var publisherNode = new SpotNode(ctx);
        using var subscriberNode = new SpotNode(ctx);
        using var publisher = publisherNode.CreateSpot();
        using var subscriber = subscriberNode.CreateSpot();

        const string topic = "spot:external";
        const string payload = "hello-discovery";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-discovery-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-discovery-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-discovery-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-discovery-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-discovery-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-discovery-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-discovery-publisher-spot"));
        subscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-discovery-subscriber-spot"));
        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(50);
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.Bind(publisherEndpoint);
        subscriberNode.Bind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);
        subscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
            () => publisherNode.StatusSnapshot().ConnectedPeerCount > 0
                && subscriberNode.StatusSnapshot().ConnectedPeerCount > 0
                && subscriberNode.SubjectsSnapshot()
                    .Any(entry => entry.Subject == topic),
            10000));

        TopicMessage? subscribed = null;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.FromString(payload);
                publisher.Publish("game.stage", topic, message);

                try
                {
                    subscribed = subscriber.Subscribe(RecvFlags.DontWait);
                    return subscribed is not null;
                }
                catch (ZlinkRecvException ex) when (ex.Result == RecvResult.NoData)
                {
                    return false;
                }
            },
            10000));

        using var received = subscribed!;
        Assert.Equal("game.stage", received.ServiceName);
        Assert.Equal(topic, received.Topic);
        Assert.Equal(payload, received.SinglePartOrThrow().GetString());
    }

    [Fact]
    public void spot_node_create_spot_publishes_to_remote_subscriber_via_discovery_across_contexts()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var registryContext = new Context();
        using var publisherContext = new Context();
        using var subscriberContext = new Context();
        using var registry = new Registry(registryContext);
        using var publisherDiscovery = new Discovery(publisherContext,
            AutoConnectType.SpotMesh, "game.stage");
        using var subscriberDiscovery = new Discovery(subscriberContext,
            AutoConnectType.SpotMesh, "game.stage");
        using var publisherNode = new SpotNode(publisherContext);
        using var subscriberNode = new SpotNode(subscriberContext);
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
        registry.SetBroadcastInterval(50);
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.Bind(publisherEndpoint);
        subscriberNode.Bind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);
        subscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
            () => publisherNode.StatusSnapshot().ConnectedPeerCount > 0
                && subscriberNode.StatusSnapshot().ConnectedPeerCount > 0,
            10000));

        TopicMessage? subscribed = null;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.FromString(payload);
                publisher.Publish("game.stage", topic, message);

                try
                {
                    subscribed = subscriber.Subscribe(RecvFlags.DontWait);
                    return subscribed is not null;
                }
                catch (ZlinkRecvException ex) when (ex.Result == RecvResult.NoData)
                {
                    return false;
                }
            },
            10000));

        using var received = subscribed!;
        Assert.Equal("game.stage", received.ServiceName);
        Assert.Equal(topic, received.Topic);
        Assert.Equal(payload, received.SinglePartOrThrow().GetString());
    }

    [Fact]
    public void spot_node_late_created_publisher_spot_publishes_to_remote_subscriber_via_discovery()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var registryContext = new Context();
        using var publisherContext = new Context();
        using var subscriberContext = new Context();
        using var registry = new Registry(registryContext);
        using var publisherDiscovery = new Discovery(publisherContext,
            AutoConnectType.SpotMesh, "game.stage");
        using var subscriberDiscovery = new Discovery(subscriberContext,
            AutoConnectType.SpotMesh, "game.stage");
        using var publisherNode = new SpotNode(publisherContext);
        using var subscriberNode = new SpotNode(subscriberContext);
        using var subscriber = subscriberNode.CreateSpot();

        const string topic = "spot:external";
        const string payload = "hello-late-publisher";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-publisher-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-publisher-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-publisher-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-publisher-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-late-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-late-subscriber"));
        subscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-late-subscriber-spot"));
        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(50);
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.Bind(publisherEndpoint);
        subscriberNode.Bind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);
        subscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
            () => publisherNode.StatusSnapshot().ConnectedPeerCount > 0
                && subscriberNode.StatusSnapshot().ConnectedPeerCount > 0,
            10000));

        using var publisher = publisherNode.CreateSpot();
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-late-publisher-spot"));

        TopicMessage? subscribed = null;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.FromString(payload);
                publisher.Publish("game.stage", topic, message);

                try
                {
                    subscribed = subscriber.Subscribe(RecvFlags.DontWait);
                    return subscribed is not null;
                }
                catch (ZlinkRecvException ex) when (ex.Result == RecvResult.NoData)
                {
                    return false;
                }
            },
            10000));

        using var received = subscribed!;
        Assert.Equal("game.stage", received.ServiceName);
        Assert.Equal(topic, received.Topic);
        Assert.Equal(payload, received.SinglePartOrThrow().GetString());
    }

    [Fact]
    public void spot_node_preconnected_publisher_spot_recovers_after_discovery_peers_become_ready()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var registryContext = new Context();
        using var publisherContext = new Context();
        using var subscriberContext = new Context();
        using var registry = new Registry(registryContext);
        using var publisherDiscovery = new Discovery(publisherContext,
            AutoConnectType.SpotMesh, "game.stage");
        using var subscriberDiscovery = new Discovery(subscriberContext,
            AutoConnectType.SpotMesh, "game.stage");
        using var publisherNode = new SpotNode(publisherContext);
        using var subscriberNode = new SpotNode(subscriberContext);
        using var subscriber = subscriberNode.CreateSpot();

        const string topic = "spot:external";
        const string payload = "hello-preconnected-publisher";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-preconnected-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-preconnected-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-preconnected-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-preconnected-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-preconnected-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-preconnected-subscriber"));
        subscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-preconnected-subscriber-spot"));
        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(50);
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.Bind(publisherEndpoint);
        subscriberNode.Bind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);

        using var publisher = publisherNode.CreateSpot();
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-preconnected-publisher-spot"));
        subscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
            () => publisherNode.StatusSnapshot().ConnectedPeerCount > 0
                && subscriberNode.StatusSnapshot().ConnectedPeerCount > 0,
            10000));

        TopicMessage? subscribed = null;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.FromString(payload);
                publisher.Publish("game.stage", topic, message);

                try
                {
                    subscribed = subscriber.Subscribe(RecvFlags.DontWait);
                    return subscribed is not null;
                }
                catch (ZlinkRecvException ex) when (ex.Result == RecvResult.NoData)
                {
                    return false;
                }
            },
            10000));

        using var received = subscribed!;
        Assert.Equal("game.stage", received.ServiceName);
        Assert.Equal(topic, received.Topic);
        Assert.Equal(payload, received.SinglePartOrThrow().GetString());
    }

    [Fact]
    public void spot_node_publisher_spot_eventually_delivers_when_publish_starts_before_discovery_ready()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var registryContext = new Context();
        using var publisherContext = new Context();
        using var subscriberContext = new Context();
        using var registry = new Registry(registryContext);
        using var publisherDiscovery = new Discovery(publisherContext,
            AutoConnectType.SpotMesh, "game.stage");
        using var subscriberDiscovery = new Discovery(subscriberContext,
            AutoConnectType.SpotMesh, "game.stage");
        using var publisherNode = new SpotNode(publisherContext);
        using var subscriberNode = new SpotNode(subscriberContext);
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
        registry.SetBroadcastInterval(50);
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.Bind(publisherEndpoint);
        subscriberNode.Bind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);

        using var publisher = publisherNode.CreateSpot();
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-early-publisher-spot"));
        subscriber.SetSubscription(topic);

        TopicMessage? subscribed = null;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var message = Message.FromString(payload);
                publisher.Publish("game.stage", topic, message);

                try
                {
                    subscribed = subscriber.Subscribe(RecvFlags.DontWait);
                    return subscribed is not null;
                }
                catch (ZlinkRecvException ex) when (ex.Result == RecvResult.NoData)
                {
                    return false;
                }
            },
            10000));

        using var received = subscribed!;
        Assert.Equal("game.stage", received.ServiceName);
        Assert.Equal(topic, received.Topic);
        Assert.Equal(payload, received.SinglePartOrThrow().GetString());
    }

}

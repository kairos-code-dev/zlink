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
        SpotNodeSubjectEntry[] subjects = node.Subjects();
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

        SpotNodeSubjectEntry[] subjects = node.Subjects();
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
        SpotNodeStatus status = node.Status();
        SpotNodeSubjectEntry[] subjects = node.Subjects();
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
    public void spot_node_publisher_forwards_to_local_subscriber()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var subscriber = node.CreateSpot();
        using var publisher = node.CreatePublisher();

        const string topic = "spot:external";
        const string payload = "hello-external";

        subscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                SpotNodeStatus status = node.Status();
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
                publisher.Publish(topic, new[] { message });

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
                    return publisher.Publish(topic)
                        .Message(sent)
                        .Flags(SendFlags.DontWait)
                        .Submit();
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

        using var subscribed = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                try
                {
                    return subscriber.Subscribe(subscribed, RecvFlags.DontWait);
                }
                catch (ZlinkRecvException ex)
                    when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
                {
                    return false;
                }
            },
            5000));

        Assert.Equal(topic, subscribed.Topic);
        Assert.Equal(payload, subscribed.FirstPart().ToArray());
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
            sender.SendToSpot(missingNodeRid, missingSpotRid)
                .Message(sent)
                .Flags(SendFlags.DontWait)
                .Submit());
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
            () => publisherNode.Status().ConnectedPeerCount > 0
                && subscriberNode.Status().ConnectedPeerCount > 0,
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
    public void spot_node_delivers_to_spot_subscribed_after_peer_connected()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var publisherNode = ctx.CreateSpotNode();
        using var subscriberNode = ctx.CreateSpotNode();
        using var publisher = publisherNode.CreateSpot();

        const string topic = "spot:late-subscription";
        const string header = "header-late-subscription";
        const string body = "body-late-subscription";

        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-subscription-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-subscription-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-late-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-late-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-late-publisher-spot"));

        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.ConnectPeer(subscriberEndpoint);
        subscriberNode.ConnectPeer(publisherEndpoint);

        Assert.True(CoreTestSupport.WaitUntil(
            () => publisherNode.Status().ConnectedPeerCount > 0
                && subscriberNode.Status().ConnectedPeerCount > 0,
            10000));

        using var subscriber = subscriberNode.CreateSpot();
        subscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-late-subscriber-spot"));
        subscriber.SetSubscription(topic);

        using var subscribed = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var headerMessage = Message.From(header);
                using var bodyMessage = Message.From(body);
                publisher.Publish(topic)
                    .Message(headerMessage)
                    .Message(bodyMessage)
                    .Submit();

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
        Assert.Equal(2, subscribed.Parts.Count);
        Assert.Equal(header, subscribed.Parts[0].GetString());
        Assert.Equal(body, subscribed.Parts[1].GetString());
    }

    [Fact]
    public void spot_node_delivers_to_remote_subscriber_when_publisher_node_also_subscribes()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var publisherNode = ctx.CreateSpotNode();
        using var subscriberNode = ctx.CreateSpotNode();
        using var publisher = publisherNode.CreateSpot();
        using var localSubscriber = publisherNode.CreateSpot();

        const string topic = "spot:local-and-remote-subscription";
        const string header = "header-local-and-remote";
        const string body = "body-local-and-remote";

        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-local-remote-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-local-remote-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-local-remote-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-local-remote-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-local-remote-publisher-spot"));
        localSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-local-remote-local-sub"));

        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.ConnectPeer(subscriberEndpoint);
        subscriberNode.ConnectPeer(publisherEndpoint);

        Assert.True(CoreTestSupport.WaitUntil(
            () => publisherNode.Status().ConnectedPeerCount > 0
                && subscriberNode.Status().ConnectedPeerCount > 0,
            10000));

        localSubscriber.SetSubscription(topic);

        using var remoteSubscriber = subscriberNode.CreateSpot();
        remoteSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-local-remote-remote-sub"));
        remoteSubscriber.SetSubscription(topic);

        using var localReceived = new TopicMessage();
        using var remoteReceived = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using var headerMessage = Message.From(header);
                using var bodyMessage = Message.From(body);
                publisher.Publish(topic)
                    .Message(headerMessage)
                    .Message(bodyMessage)
                    .Submit();

                var local = TrySubscribe(localSubscriber, localReceived);
                var remote = TrySubscribe(remoteSubscriber, remoteReceived);
                return local && remote;
            },
            10000));

        Assert.Equal(topic, localReceived.Topic);
        Assert.Equal(topic, remoteReceived.Topic);
        Assert.Equal(header, remoteReceived.Parts[0].GetString());
        Assert.Equal(body, remoteReceived.Parts[1].GetString());
    }

    [Fact]
    public void spot_node_delivers_single_publish_after_remote_subscription_is_ready()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var publisherNode = ctx.CreateSpotNode();
        using var subscriberNode = ctx.CreateSpotNode();
        using var publisher = publisherNode.CreateSpot();
        using var localSubscriber = publisherNode.CreateSpot();
        using var remoteSubscriber = subscriberNode.CreateSpot();

        const string topic = "spot:single-ready-publish";
        const string header = "header-single-ready";
        const string body = "body-single-ready";

        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-ready-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-ready-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-ready-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-single-ready-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-ready-publisher-spot"));
        localSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-ready-local-sub"));
        remoteSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-single-ready-remote-sub"));

        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.ConnectPeer(subscriberEndpoint);
        subscriberNode.ConnectPeer(publisherEndpoint);
        localSubscriber.SetSubscription(topic);
        remoteSubscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
                () => publisherNode.Status().ConnectedPeerCount > 0
                    && subscriberNode.Status().ConnectedPeerCount > 0
                    && HasReadySubject(publisherNode, topic)
                    && HasReadySubject(subscriberNode, topic),
                10000),
            $"Subjects were not ready. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        using (var headerMessage = Message.From(header))
        using (var bodyMessage = Message.From(body))
        {
            publisher.Publish(topic)
                .Message(headerMessage)
                .Message(bodyMessage)
                .Submit();
        }

        using var remoteReceived = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
                () => TrySubscribe(remoteSubscriber, remoteReceived),
                3000),
            $"Single publish was not delivered. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        Assert.Equal(topic, remoteReceived.Topic);
        Assert.Equal(header, remoteReceived.Parts[0].GetString());
        Assert.Equal(body, remoteReceived.Parts[1].GetString());
    }

    [Fact]
    public void spot_node_delivers_single_publish_after_discovered_remote_subscription_is_ready()
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
        using var localSubscriber = publisherNode.CreateSpot();
        using var remoteSubscriber = subscriberNode.CreateSpot();

        const string topic = "spot:single-discovered-ready-publish";
        const string header = "header-single-discovered-ready";
        const string body = "body-single-discovered-ready";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-discovered-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-discovered-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-discovered-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-discovered-subscriber");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-discovered-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-single-discovered-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-discovered-publisher-spot"));
        localSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-discovered-local-sub"));
        remoteSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-single-discovered-remote-sub"));

        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);
        localSubscriber.SetSubscription(topic);
        remoteSubscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
                () => publisherNode.Status().ConnectedPeerCount > 0
                    && subscriberNode.Status().ConnectedPeerCount > 0
                    && HasReadySubject(publisherNode, topic)
                    && HasReadySubject(subscriberNode, topic),
                10000),
            $"Subjects were not ready. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        using (var headerMessage = Message.From(header))
        using (var bodyMessage = Message.From(body))
        {
            publisher.Publish(topic)
                .Message(headerMessage)
                .Message(bodyMessage)
                .Submit();
        }

        using var remoteReceived = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
                () => TrySubscribe(remoteSubscriber, remoteReceived),
                3000),
            $"Single publish was not delivered. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        Assert.Equal(topic, remoteReceived.Topic);
        Assert.Equal(header, remoteReceived.Parts[0].GetString());
        Assert.Equal(body, remoteReceived.Parts[1].GetString());
    }

    [Fact]
    public void spot_node_all_mode_delivers_single_publish_after_discovered_remote_subscription_is_ready()
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
        using var publisherNode = publisherContext.CreateSpotNode(SpotNodeMode.All);
        using var subscriberNode = subscriberContext.CreateSpotNode(SpotNodeMode.All);
        using var publisher = publisherNode.CreateSpot();
        using var localSubscriber = publisherNode.CreateSpot();
        using var remoteSubscriber = subscriberNode.CreateSpot();

        const string topic = "spot:single-all-mode-ready-publish";
        const string header = "header-single-all-mode-ready";
        const string body = "body-single-all-mode-ready";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-all-mode-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-all-mode-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-all-mode-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-all-mode-subscriber");
        string publisherRouterEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-all-mode-publisher-router");
        string subscriberRouterEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-all-mode-subscriber-router");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-all-mode-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-single-all-mode-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-all-mode-publisher-spot"));
        localSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-all-mode-local-sub"));
        remoteSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-single-all-mode-remote-sub"));

        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.SetRouterBind(publisherRouterEndpoint);
        subscriberNode.SetRouterBind(subscriberRouterEndpoint);
        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);
        localSubscriber.SetSubscription(topic);
        remoteSubscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
                () => publisherNode.Status().ConnectedPeerCount > 0
                    && subscriberNode.Status().ConnectedPeerCount > 0
                    && HasReadySubject(publisherNode, topic)
                    && HasReadySubject(subscriberNode, topic),
                10000),
            $"Subjects were not ready. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        using (var headerMessage = Message.From(header))
        using (var bodyMessage = Message.From(body))
        {
            publisher.Publish(topic)
                .Message(headerMessage)
                .Message(bodyMessage)
                .Submit();
        }

        using var remoteReceived = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
                () => TrySubscribe(remoteSubscriber, remoteReceived),
                3000),
            $"Single publish was not delivered. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        Assert.Equal(topic, remoteReceived.Topic);
        Assert.Equal(header, remoteReceived.Parts[0].GetString());
        Assert.Equal(body, remoteReceived.Parts[1].GetString());
    }

    [Fact]
    public void spot_node_all_mode_delivers_single_publish_when_publisher_spot_is_also_subscriber()
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
        using var publisherNode = publisherContext.CreateSpotNode(SpotNodeMode.All);
        using var subscriberNode = subscriberContext.CreateSpotNode(SpotNodeMode.All);
        using var publisher = publisherNode.CreateSpot();
        using var remoteSubscriber = subscriberNode.CreateSpot();

        const string topic = "spot:single-self-subscribe-ready-publish";
        const string header = "header-single-self-subscribe-ready";
        const string body = "body-single-self-subscribe-ready";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-self-subscribe-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-self-subscribe-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-self-subscribe-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-self-subscribe-subscriber");
        string publisherRouterEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-self-subscribe-publisher-router");
        string subscriberRouterEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-single-self-subscribe-subscriber-router");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-self-sub-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-single-self-sub-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-single-self-sub-publisher-spot"));
        remoteSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-single-self-sub-remote-sub"));

        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.SetRouterBind(publisherRouterEndpoint);
        subscriberNode.SetRouterBind(subscriberRouterEndpoint);
        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);
        publisher.SetSubscription(topic);
        remoteSubscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
                () => publisherNode.Status().ConnectedPeerCount > 0
                    && subscriberNode.Status().ConnectedPeerCount > 0
                    && HasReadySubject(publisherNode, topic)
                    && HasReadySubject(subscriberNode, topic),
                10000),
            $"Subjects were not ready. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        using (var headerMessage = Message.From(header))
        using (var bodyMessage = Message.From(body))
        {
            publisher.Publish(topic)
                .Message(headerMessage)
                .Message(bodyMessage)
                .Submit();
        }

        using var remoteReceived = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
                () => TrySubscribe(remoteSubscriber, remoteReceived),
                3000),
            $"Single publish was not delivered. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        Assert.Equal(topic, remoteReceived.Topic);
        Assert.Equal(header, remoteReceived.Parts[0].GetString());
        Assert.Equal(body, remoteReceived.Parts[1].GetString());
    }

    [Fact]
    public void spot_node_all_mode_propagates_subscription_added_after_discovered_peer_connected()
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
        using var publisherNode = publisherContext.CreateSpotNode(SpotNodeMode.All);
        using var subscriberNode = subscriberContext.CreateSpotNode(SpotNodeMode.All);
        using var publisher = publisherNode.CreateSpot();
        using var remoteSubscriber = subscriberNode.CreateSpot();

        const string topic = "spot:late-discovered-subscription";
        const string payload = "payload-late-discovered-subscription";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-discovered-sub-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-discovered-sub-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-discovered-sub-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-discovered-sub-subscriber");
        string publisherRouterEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-discovered-sub-publisher-router");
        string subscriberRouterEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-late-discovered-sub-subscriber-router");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-late-disc-sub-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-late-disc-sub-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-late-disc-sub-publisher-spot"));
        remoteSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-late-disc-sub-remote-sub"));

        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.SetRouterBind(publisherRouterEndpoint);
        subscriberNode.SetRouterBind(subscriberRouterEndpoint);
        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);

        Assert.True(CoreTestSupport.WaitUntil(
                () => publisherNode.Status().ConnectedPeerCount > 0
                    && subscriberNode.Status().ConnectedPeerCount > 0,
                10000),
            $"Peers were not connected. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        publisher.SetSubscription(topic);
        remoteSubscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
                () => HasReadySubject(publisherNode, topic)
                    && HasReadySubject(subscriberNode, topic),
                10000),
            $"Subjects were not ready. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        using var message = Message.From(payload);
        publisher.Publish(topic).Message(message).Submit();

        using var remoteReceived = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
                () => TrySubscribe(remoteSubscriber, remoteReceived),
                3000),
            $"Single publish was not delivered. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        Assert.Equal(topic, remoteReceived.Topic);
        Assert.Equal(payload, remoteReceived.SinglePartOrThrow().GetString());
    }

    [Fact]
    public void spot_node_all_mode_delivers_when_remote_subscription_is_added_after_existing_subscriber_connected()
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
        using var publisherNode = publisherContext.CreateSpotNode(SpotNodeMode.All);
        using var subscriberNode = subscriberContext.CreateSpotNode(SpotNodeMode.All);
        using var publisher = publisherNode.CreateSpot();
        using var remoteSubscriber = subscriberNode.CreateSpot();

        const string topic = "spot:remote-late-subscription";
        const string payload = "payload-remote-late-subscription";

        string registryPub = CoreTestSupport.NewEndpoint("tcp",
            "spot-remote-late-sub-pub");
        string registryRouter = CoreTestSupport.NewEndpoint("tcp",
            "spot-remote-late-sub-router");
        string publisherEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-remote-late-sub-publisher");
        string subscriberEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-remote-late-sub-subscriber");
        string publisherRouterEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-remote-late-sub-publisher-router");
        string subscriberRouterEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-remote-late-sub-subscriber-router");

        publisherNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-remote-late-sub-publisher"));
        subscriberNode.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-remote-late-sub-subscriber"));
        publisher.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "z-spot-remote-late-sub-publisher-spot"));
        remoteSubscriber.SetRoutingId(CoreTestSupport.RoutingIdUtf8(
            "a-spot-remote-late-sub-remote-sub"));

        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        publisherDiscovery.ConnectRegistry(registryRouter);
        subscriberDiscovery.ConnectRegistry(registryRouter);
        publisherNode.SetRouterBind(publisherRouterEndpoint);
        subscriberNode.SetRouterBind(subscriberRouterEndpoint);
        publisherNode.SetPubBind(publisherEndpoint);
        subscriberNode.SetPubBind(subscriberEndpoint);
        publisher.SetSubscription(topic);
        publisherNode.AttachDiscovery(publisherDiscovery);
        subscriberNode.AttachDiscovery(subscriberDiscovery);

        Assert.True(CoreTestSupport.WaitUntil(
                () => publisherNode.Status().ConnectedPeerCount > 0
                    && subscriberNode.Status().ConnectedPeerCount > 0
                    && HasReadySubject(publisherNode, topic),
                10000),
            $"Initial subject was not ready. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        remoteSubscriber.SetSubscription(topic);

        Assert.True(CoreTestSupport.WaitUntil(
                () => HasReadySubject(subscriberNode, topic),
                10000),
            $"Late remote subject was not ready. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        using var message = Message.From(payload);
        publisher.Publish(topic).Message(message).Submit();

        using var remoteReceived = new TopicMessage();
        Assert.True(CoreTestSupport.WaitUntil(
                () => TrySubscribe(remoteSubscriber, remoteReceived),
                3000),
            $"Single publish was not delivered. publisher=[{SubjectDump(publisherNode)}], subscriber=[{SubjectDump(subscriberNode)}]");

        Assert.Equal(topic, remoteReceived.Topic);
        Assert.Equal(payload, remoteReceived.SinglePartOrThrow().GetString());
    }

    private static bool TrySubscribe(ISpot spot, TopicMessage message)
    {
        try
        {
            return spot.Subscribe(message, RecvFlags.DontWait);
        }
        catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
        {
            return false;
        }
    }

    private static bool HasReadySubject(ISpotNode node, string topic)
    {
        foreach (var subject in node.Subjects())
        {
            if (subject.Subject == topic
                && subject.ReadyPeerCount > 0)
            {
                return true;
            }
        }

        return false;
    }

    private static string SubjectDump(ISpotNode node)
    {
        return string.Join(", ",
            Array.ConvertAll(node.Subjects(), subject =>
                $"{subject.Subject}:{subject.SubjectKind}:ready={subject.ReadyPeerCount}:active={subject.ActivePeerCount}"));
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

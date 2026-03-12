using System;
using Xunit;

namespace Zlink.Tests;

public sealed class test_spot_pubsub_basic
{
    [Fact]
    public void spot_local_pubsub()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        node.Bind(CoreTestSupport.NewEndpoint("inproc", "spot-local"));
        using var pub = node.GetPubSocket();
        using var sub = node.GetSubSocket();

        Assert.NotNull(pub);
        Assert.NotNull(sub);
    }

    [Fact]
    public void spot_pattern_subscribe()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        spot.SubscribePattern("zone:12:*");
        spot.Unsubscribe("zone:12:*");
    }

    [Fact]
    public void spot_multipart_publish()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var nodeA = new SpotNode(ctx);
        using var nodeB = new SpotNode(ctx);
        string epA = CoreTestSupport.NewEndpoint("inproc", "spot-mp-a");
        string epB = CoreTestSupport.NewEndpoint("inproc", "spot-mp-b");
        nodeA.Bind(epA);
        nodeB.Bind(epB);
        nodeB.ConnectPeerPub(epA);

        using var spotA = new Spot(nodeA);
        using var spotB = new Spot(nodeB);
        spotB.Subscribe("mp:topic");
        Assert.True(CoreTestSupport.WaitUntil(() => nodeB.GetSubPeers().Length > 0,
            2000));

        spotA.Publish("mp:topic", new[]
        {
            Message.FromBytes("one"u8),
            Message.FromBytes("two"u8)
        });

        SpotMessage received = CoreTestSupport.ReceiveSpotWithTimeout(spotB, 2000);
        try
        {
            Assert.Equal("mp:topic", received.TopicId);
            Assert.Equal(2, received.Parts.Length);
            Assert.Equal("one", CoreTestSupport.Utf8(received.Parts[0]));
            Assert.Equal("two", CoreTestSupport.Utf8(received.Parts[1]));
        }
        finally
        {
            foreach (Message part in received.Parts)
                part.Dispose();
        }
    }

    [Fact]
    public void spot_publish_without_subscribers()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        spot.Publish("metrics:cpu", "nop"u8, SendFlags.None);
    }

    [Fact]
    public void spot_publish_message_moves_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);
        spot.Subscribe("own:topic");

        using var msg = Message.FromBytes("owned-spot"u8);
        spot.Publish("own:topic", new[] { msg }, SendFlags.None);

        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = msg.Size;
        });
    }

    [Fact]
    public void spot_single_payload_receive()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var nodeA = new SpotNode(ctx);
        using var nodeB = new SpotNode(ctx);
        string epA = CoreTestSupport.NewEndpoint("inproc", "spot-raw-a");
        string epB = CoreTestSupport.NewEndpoint("inproc", "spot-raw-b");
        nodeA.Bind(epA);
        nodeB.Bind(epB);
        nodeB.ConnectPeerPub(epA);

        using var publisher = new Spot(nodeA);
        using var subscriber = new Spot(nodeB);
        subscriber.Subscribe("raw:topic");
        Assert.True(CoreTestSupport.WaitUntil(() => nodeB.GetSubPeers().Length > 0,
            2000));

        publisher.Publish("raw:topic", "raw-payload"u8, SendFlags.None);

        Span<byte> payload = stackalloc byte[64];
        int size = subscriber.ReceiveSinglePayload(payload, ReceiveFlags.None);
        Assert.Equal("raw-payload", CoreTestSupport.Utf8(payload[..size]));
    }

    [Fact]
    public void spot_peer_pubsub()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var nodeA = new SpotNode(ctx);
        using var nodeB = new SpotNode(ctx);

        string epA = CoreTestSupport.NewEndpoint("inproc", "spot-a");
        string epB = CoreTestSupport.NewEndpoint("inproc", "spot-b");
        nodeA.Bind(epA);
        nodeB.Bind(epB);
        nodeB.ConnectPeerPub(epA);

        using var spotA = new Spot(nodeA);
        using var spotB = new Spot(nodeB);
        spotB.Subscribe("peer:topic");
        Assert.True(CoreTestSupport.WaitUntil(() => nodeB.GetSubPeers().Length > 0,
            2000));

        spotA.Publish("peer:topic", "pong"u8, SendFlags.None);

        SpotMessage received = CoreTestSupport.ReceiveSpotWithTimeout(spotB, 2000);
        try
        {
            Assert.Equal("peer:topic", received.TopicId);
            Assert.Single(received.Parts);
            Assert.Equal("pong", CoreTestSupport.Utf8(received.Parts[0]));
        }
        finally
        {
            foreach (Message part in received.Parts)
                part.Dispose();
        }
    }

    [Fact]
    public void spot_topic_input_validation()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        string tooLongTopic = new string('a', 256);

        Assert.Throws<ArgumentException>(() =>
            spot.Publish("", "x"u8, SendFlags.None));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.Publish(tooLongTopic, "x"u8, SendFlags.None));

        Assert.Throws<ArgumentException>(() => spot.Subscribe(""));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.Subscribe(tooLongTopic));

        Assert.Throws<ArgumentException>(() => spot.SubscribePattern(""));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.SubscribePattern(tooLongTopic));

        Assert.Throws<ArgumentException>(() => spot.Unsubscribe(""));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.Unsubscribe(tooLongTopic));
    }

    [Fact]
    public void spot_default_pub_socket_handle_is_available()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        node.Bind(CoreTestSupport.NewEndpoint("inproc", "spot-mode-pub"));
        using var socket = node.GetPubSocket();
        Assert.NotNull(socket);
    }

    [Fact]
    public void spot_default_sub_socket_handle_is_available()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        node.Bind(CoreTestSupport.NewEndpoint("inproc", "spot-mode-sub"));
        using var socket = node.GetSubSocket();
        Assert.NotNull(socket);
    }
}

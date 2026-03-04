using System;
using System.Threading;
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
        using var spot = new Spot(node);

        spot.Subscribe("chat:room1:msg");
        spot.Publish("chat:room1:msg", "hello"u8, SendFlags.None);

        SpotMessage recv = CoreTestSupport.ReceiveSpotWithTimeout(spot, 2000);
        Assert.Equal("chat:room1:msg", recv.TopicId);
        Assert.Single(recv.Parts);
        Assert.Equal("hello", CoreTestSupport.Utf8(recv.Parts[0]));
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
        spot.Publish("zone:12:state", "ping"u8, SendFlags.None);

        SpotMessage recv = CoreTestSupport.ReceiveSpotWithTimeout(spot, 2000);
        Assert.Equal("zone:12:state", recv.TopicId);
        Assert.Single(recv.Parts);
        Assert.Equal("ping", CoreTestSupport.Utf8(recv.Parts[0]));
    }

    [Fact]
    public void spot_multipart_publish()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        spot.Subscribe("mp:topic");
        spot.Publish("mp:topic", new[]
        {
            Message.FromBytes("one"u8),
            Message.FromBytes("two"u8)
        });

        SpotMessage recv = CoreTestSupport.ReceiveSpotWithTimeout(spot, 2000);
        Assert.Equal("mp:topic", recv.TopicId);
        Assert.Equal(2, recv.Parts.Length);
        Assert.Equal("one", CoreTestSupport.Utf8(recv.Parts[0]));
        Assert.Equal("two", CoreTestSupport.Utf8(recv.Parts[1]));
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
    public void spot_packet_handler_zero_copy_callback()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        using var signal = new ManualResetEventSlim(false);
        string? topicSeen = null;
        string? payloadSeen = null;
        int partCount = 0;

        spot.SetPacketHandler((topicUtf8, parts) =>
        {
            topicSeen = CoreTestSupport.Utf8(topicUtf8);
            partCount = parts.Length;
            if (parts.Length > 0)
                payloadSeen = CoreTestSupport.Utf8(parts[0].AsReadOnlySpan());
            signal.Set();
        });

        spot.Subscribe("raw:topic");
        using var msg = Message.FromBytes("raw-payload"u8);
        spot.Publish("raw:topic", new[] { msg }, SendFlags.None);

        Assert.True(signal.Wait(2000));
        Assert.NotNull(topicSeen);
        Assert.NotNull(payloadSeen);
        Assert.Equal("raw:topic", topicSeen);
        Assert.Equal(1, partCount);
        Assert.Equal("raw-payload", payloadSeen);
    }

    [Fact]
    public void spot_managed_handler_copies_payload()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        using var signal = new ManualResetEventSlim(false);
        string? topicSeen = null;
        string? payloadSeen = null;

        spot.SetHandler((topic, parts) =>
        {
            topicSeen = topic;
            if (parts.Length > 0)
                payloadSeen = CoreTestSupport.Utf8(parts[0]);
            foreach (Message part in parts)
                part.Dispose();
            signal.Set();
        });

        spot.Subscribe("copy:topic");
        spot.Publish("copy:topic", "copy-payload"u8, SendFlags.None);

        Assert.True(signal.Wait(2000));
        Assert.Equal("copy:topic", topicSeen);
        Assert.Equal("copy-payload", payloadSeen);
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

        using var spotB = new Spot(nodeB);
        spotB.Subscribe("peer:topic");
        Thread.Sleep(100);

        using var spotA = new Spot(nodeA);
        spotA.Publish("peer:topic", "pong"u8, SendFlags.None);

        SpotMessage recv = CoreTestSupport.ReceiveSpotWithTimeout(spotB, 2000);
        Assert.Equal("peer:topic", recv.TopicId);
        Assert.Single(recv.Parts);
        Assert.Equal("pong", CoreTestSupport.Utf8(recv.Parts[0]));
    }
}

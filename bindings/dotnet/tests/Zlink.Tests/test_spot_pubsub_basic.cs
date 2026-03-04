using System;
using System.Text;
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
        spot.Publish("chat:room1:msg",
            new[] { Message.FromBytes(Encoding.UTF8.GetBytes("hello")) });

        SpotMessage recv = CoreTestSupport.ReceiveSpotWithTimeout(spot, 2000);
        Assert.Equal("chat:room1:msg", recv.TopicId);
        Assert.Single(recv.Parts);
        Assert.Equal("hello", Encoding.UTF8.GetString(recv.Parts[0].ToArray()));
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
        spot.Publish("zone:12:state",
            new[] { Message.FromBytes(Encoding.UTF8.GetBytes("ping")) });

        SpotMessage recv = CoreTestSupport.ReceiveSpotWithTimeout(spot, 2000);
        Assert.Equal("zone:12:state", recv.TopicId);
        Assert.Single(recv.Parts);
        Assert.Equal("ping", Encoding.UTF8.GetString(recv.Parts[0].ToArray()));
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
            Message.FromBytes(Encoding.UTF8.GetBytes("one")),
            Message.FromBytes(Encoding.UTF8.GetBytes("two"))
        });

        SpotMessage recv = CoreTestSupport.ReceiveSpotWithTimeout(spot, 2000);
        Assert.Equal("mp:topic", recv.TopicId);
        Assert.Equal(2, recv.Parts.Length);
        Assert.Equal("one", Encoding.UTF8.GetString(recv.Parts[0].ToArray()));
        Assert.Equal("two", Encoding.UTF8.GetString(recv.Parts[1].ToArray()));
    }

    [Fact]
    public void spot_publish_without_subscribers()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        spot.Publish("metrics:cpu",
            new[] { Message.FromBytes(Encoding.UTF8.GetBytes("nop")) });
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
        spotA.Publish("peer:topic",
            new[] { Message.FromBytes(Encoding.UTF8.GetBytes("pong")) });

        SpotMessage recv = CoreTestSupport.ReceiveSpotWithTimeout(spotB, 2000);
        Assert.Equal("peer:topic", recv.TopicId);
        Assert.Single(recv.Parts);
        Assert.Equal("pong", Encoding.UTF8.GetString(recv.Parts[0].ToArray()));
    }
}

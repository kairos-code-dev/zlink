using System;
using Xunit;
using Zlink.Service;

namespace Zlink.Tests;

public sealed class test_spot_pubsub_basic
{
    [Fact]
    public void spot_subscription_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var spot = new Spot(ctx);

        spot.SetSubscription("zone:12:*");
        spot.UnsetSubscription("zone:12:*");
    }

    [Fact]
    public void spot_single_part_publish_moves_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var spot = new Spot(ctx);
        spot.SetSubscription("own:topic");

        using var msg = Message.FromString("owned-spot");
        spot.Publish("own:topic", msg);

        Assert.Throws<ObjectDisposedException>(() => _ = msg.Size);
    }

    [Fact]
    public void spot_subscribe_handler_blocks_direct_subscribe_path()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var spot = new Spot(ctx);
        spot.SetSubscription("cb:topic");
        spot.SubscribeHandler((topic, parts) =>
        {
            foreach (Message part in parts)
                part.Dispose();
        });

        Assert.Throws<ZlinkException>(() =>
            spot.Subscribe(out _, out Message _, ReceiveFlags.DontWait));
    }

    [Fact]
    public void spot_topic_input_validation()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var spot = new Spot(ctx);

        string tooLongTopic = new string('a', 256);

        Assert.Throws<ArgumentException>(() => spot.SetSubscription(""));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.SetSubscription(tooLongTopic));
        Assert.Throws<ArgumentException>(() => spot.UnsetSubscription(""));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.UnsetSubscription(tooLongTopic));
        Assert.Throws<ArgumentException>(() =>
            spot.Publish("", Message.FromString("x")));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.Publish(tooLongTopic, Message.FromString("x")));
    }
}

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
}

using System;
using Xunit;

namespace Zlink.Tests;

public sealed class test_domain_objects
{
    [Fact]
    public void routing_id_validates_utf8_and_length()
    {
        RoutingId plain = new("dealer-1");
        RoutingId hex = new("hex:0102A0FF");

        Assert.Equal("dealer-1", plain.Value);
        Assert.Equal("hex:0102A0FF", hex.Value);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            _ = new RoutingId(new string('a', 256)));
        Assert.Throws<ArgumentException>(() => _ = new RoutingId("hex:ABC"));
    }

    [Fact]
    public void received_single_part_or_throw_enforces_shape()
    {
        using var part = Message.FromString("one");
        var received = new Received("route-a", new[] { part.Move() });

        Assert.True(received.HasSinglePart);
        Assert.Equal("route-a", received.RoutingIdValue?.Value);
        using Message single = received.SinglePartOrThrow();
        Assert.Equal("one", single.GetString());
    }

    [Fact]
    public void topic_message_single_part_or_throw_enforces_shape()
    {
        using var first = Message.FromString("a");
        using var second = Message.FromString("b");
        var topicMessage = new Subscribed("route-b", "prices",
            new[] { first.Move(), second.Move() });

        Assert.False(topicMessage.HasSinglePart);
        Assert.Equal("prices", topicMessage.Topic);
        Assert.Equal("route-b", topicMessage.RoutingIdValue?.Value);
        Assert.Throws<InvalidOperationException>(() =>
            topicMessage.SinglePartOrThrow());
    }
}

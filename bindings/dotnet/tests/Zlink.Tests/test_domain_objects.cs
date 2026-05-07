using System;
using System.Text;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_domain_objects
{
    [Fact]
    public void routing_id_validates_utf8_and_length()
    {
        RoutingId plain = RoutingId.FromBytes(Encoding.UTF8.GetBytes("dealer-1"));
        RoutingId binary = RoutingId.FromBytes(new byte[] { 0x01, 0x02, 0xA0, 0xFF });
        RoutingId parsed = RoutingId.FromString("0102A0ff");

        Assert.Equal("dealer-1", plain.ToString());
        Assert.Equal("0102a0ff", binary.ToHex());
        Assert.Equal(binary, parsed);
        Assert.Equal(255, RoutingId.FromString(new string('a', 510)).Size);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            _ = RoutingId.FromBytes(Encoding.UTF8.GetBytes(new string('a', 256))));
        Assert.Throws<ArgumentException>(() =>
            _ = RoutingId.FromString("not-hex"));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            _ = RoutingId.FromString(new string('a', 512)));
    }

    [Fact]
    public void received_single_part_or_throw_enforces_shape()
    {
        using var part = Message.FromString("one");
        var received = new Received(CoreTestSupport.RoutingIdUtf8("route-a"),
            new[] { part.Move() });

        Assert.True(received.IsSinglePart);
        Assert.Equal("route-a", received.RoutingId?.ToString());
        using Message single = received.SinglePartOrThrow();
        Assert.Equal("one", single.GetString());
    }

    [Fact]
    public void topic_message_single_part_or_throw_enforces_shape()
    {
        using var first = Message.FromString("a");
        using var second = Message.FromString("b");
        var topicMessage = new TopicMessage(CoreTestSupport.RoutingIdUtf8("route-b"),
            "svc:prices", "prices", new[] { first.Move(), second.Move() });

        Assert.False(topicMessage.IsSinglePart);
        Assert.Equal("svc:prices", topicMessage.ServiceName);
        Assert.Equal("prices", topicMessage.Topic);
        Assert.Equal("route-b", topicMessage.RoutingId?.ToString());
        Assert.Throws<ZlinkRecvException>(() =>
            topicMessage.SinglePartOrThrow());
    }

    [Fact]
    public void actor_ref_keeps_generation_zero_as_unchecked_ref()
    {
        ActorRef actor = ActorRef.Unchecked(
            CoreTestSupport.RoutingIdUtf8("node-a"), "actor-a");

        Assert.True(actor.IsUnchecked);
        Assert.Equal(0UL, actor.Generation);
        Assert.Equal("actor-a", actor.ActorId);
        Assert.Equal("node-a", actor.NodeRid.ToString());
    }
}

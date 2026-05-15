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

        Assert.Equal(plain.ToHex(), plain.ToString());
        Assert.Equal("6465616c65722d31", plain.ToString());
        Assert.Equal("0102a0ff", binary.ToHex());
        Assert.Equal(binary.ToHex(), binary.ToString());
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
    public void routing_id_conversion_remains_stable_across_many_values()
    {
        for (int i = 0; i < 5000; i++)
        {
            byte[] bytes = BitConverter.GetBytes(i);
            if (BitConverter.IsLittleEndian)
                Array.Reverse(bytes);

            RoutingId routingId = RoutingId.FromBytes(bytes);
            Assert.Equal(Convert.ToHexString(bytes).ToLowerInvariant(),
                routingId.ToHex());
        }

        RoutingId final = RoutingId.FromBytes(Encoding.UTF8.GetBytes("stable"));
        Assert.Equal("737461626c65", final.ToHex());
    }

    [Fact]
    public void actor_ref_keeps_generation_zero_as_unchecked_ref()
    {
        ActorRef actor = new(CoreTestSupport.RoutingIdUtf8("node-a"),
            "actor-a", generation: 0);

        Assert.True(actor.IsUnchecked);
        Assert.Equal(0UL, actor.Generation);
        Assert.Equal("actor-a", actor.ActorId);
        Assert.Equal(CoreTestSupport.RoutingIdUtf8("node-a").ToHex(),
            actor.NodeRid.ToString());
    }
}

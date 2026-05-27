using System;
using System.Buffers.Binary;
using System.Text;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_domain_objects
{
    [Fact]
    public void routing_id_validates_utf8_and_length()
    {
        RoutingId plain = RoutingId.From(Encoding.UTF8.GetBytes("dealer-1"));
        RoutingId fromString = RoutingId.From("dealer-1");
        RoutingId sameString = RoutingId.From("dealer-1");
        RoutingId binary = RoutingId.From(new byte[] { 0x01, 0x02, 0xA0, 0xFF });
        RoutingId parsed = RoutingId.FromHex("0102A0ff");

        Assert.Equal("6465616c65722d31", plain.ToHex());
        Assert.Equal("dealer-1", plain.ToString());
        Assert.Equal(plain, fromString);
        Assert.Equal(plain, sameString);
        Assert.Equal("0102a0ff", binary.ToHex());
        Assert.Equal("16949503", binary.ToString());
        Assert.Equal(binary, parsed);
        Assert.Equal(255, RoutingId.FromHex(new string('a', 510)).Size);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            _ = RoutingId.From(Encoding.UTF8.GetBytes(new string('a', 256))));
        Assert.Throws<ArgumentException>(() =>
            _ = RoutingId.FromHex("not-hex"));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            _ = RoutingId.FromHex(new string('a', 512)));
    }

    [Fact]
    public void routing_id_supports_uint32_and_guid_display()
    {
        RoutingId number = RoutingId.From(23u);
        Assert.Equal("00000017", number.ToHex());
        Assert.Equal("23", number.ToString());
        Assert.True(number.TryToUInt32(out uint value));
        Assert.Equal(23U, value);

        Guid guid = Guid.Parse("550e8400-e29b-41d4-a716-446655440000");
        RoutingId guidRoutingId = RoutingId.From(guid);
        Assert.Equal("550e8400e29b41d4a716446655440000",
            guidRoutingId.ToHex());
        Assert.Equal("550e8400-e29b-41d4-a716-446655440000",
            guidRoutingId.ToString());
        Assert.True(guidRoutingId.TryToGuid(out Guid parsed));
        Assert.Equal(guid, parsed);

        byte[] intBytes = new byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(intBytes, 23);
        Assert.Equal(number, RoutingId.From(intBytes));
    }

    [Fact]
    public void routing_id_conversion_remains_stable_across_many_values()
    {
        for (int i = 0; i < 5000; i++)
        {
            byte[] bytes = BitConverter.GetBytes(i);
            if (BitConverter.IsLittleEndian)
                Array.Reverse(bytes);

            RoutingId routingId = RoutingId.From(bytes);
            Assert.Equal(Convert.ToHexString(bytes).ToLowerInvariant(),
                routingId.ToHex());
        }

        RoutingId final = RoutingId.From(Encoding.UTF8.GetBytes("stable"));
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
        Assert.Equal(CoreTestSupport.RoutingIdUtf8("node-a").ToString(),
            actor.NodeRid.ToString());
    }
}

using MessagePack;
using Systems.Zlink.Codecs.MessagePack;

namespace Systems.Zlink.Codecs.Tests;

public sealed class test_messagepack_message_extensions
{
    [Fact]
    public void messagepack_to_message_and_parse_messagepack_round_trip_payload()
    {
        var expected = new MessagePackRoundtripValue
        {
            Id = 11,
            Name = "codec-messagepack"
        };

        using Message message = expected.ToMsgPack();
        MessagePackRoundtripValue actual = message.FromMsgPack<MessagePackRoundtripValue>();

        Assert.Equal(expected.Id, actual.Id);
        Assert.Equal(expected.Name, actual.Name);
    }
}

[MessagePackObject]
public sealed class MessagePackRoundtripValue
{
    [Key(0)]
    public int Id { get; set; }

    [Key(1)]
    public string Name { get; set; } = string.Empty;
}

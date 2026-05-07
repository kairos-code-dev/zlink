using Google.Protobuf.WellKnownTypes;
using Systems.Zlink.Codecs.Protobuf;

namespace Systems.Zlink.Codecs.Tests;

public sealed class test_protobuf_message_extensions
{
    [Fact]
    public void protobuf_to_message_and_parse_proto_round_trip_payload()
    {
        var expected = new StringValue
        {
            Value = "codec-protobuf"
        };

        using Message message = expected.ToProto();
        StringValue actual = message.FromProto<StringValue>();

        Assert.Equal(expected.Value, actual.Value);
    }
}

using Systems.Zlink.Codecs.Json;

namespace Systems.Zlink.Codecs.Tests;

public sealed class test_json_message_extensions
{
    [Fact]
    public void json_to_message_and_parse_json_round_trip_payload()
    {
        var expected = new JsonRoundtripValue(7, "codec-json");

        using Message message = expected.ToJson();
        JsonRoundtripValue actual = message.FromJson<JsonRoundtripValue>();

        Assert.Equal(expected, actual);
    }

    public sealed record JsonRoundtripValue(int Id, string Name);
}

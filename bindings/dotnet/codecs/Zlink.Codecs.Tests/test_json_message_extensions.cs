using Systems.Zlink.Codecs.Json;
using System.Text;

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

    [Fact]
    public void parse_json_uses_default_web_options()
    {
        using Message message = Message.From("""{"accessToken":"player-1"}""");

        JsonWebOptionsValue actual = message.FromJson<JsonWebOptionsValue>();

        Assert.Equal("player-1", actual.AccessToken);
    }

    [Fact]
    public void json_to_message_uses_default_web_options()
    {
        using Message message = new JsonWebOptionsValue("player-1").ToJson();
        string payload = Encoding.UTF8.GetString(message.AsReadOnlySpan());

        Assert.Contains("\"accessToken\"", payload);
    }

    public sealed record JsonRoundtripValue(int Id, string Name);

    public sealed record JsonWebOptionsValue(string AccessToken);
}

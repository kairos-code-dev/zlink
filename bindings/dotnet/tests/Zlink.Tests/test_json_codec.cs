using Systems.Zlink.Codecs.Json;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_json_codec
{
    [Fact]
    public void json_message_decode_uses_web_defaults()
    {
        using var message = Message.From("""{"accessToken":"player-1"}""");

        var decoded = message.Decode<JsonCodecPayload>();

        Assert.Equal("player-1", decoded.AccessToken);
    }

    [Fact]
    public void json_message_encode_uses_web_defaults()
    {
        using var message = new JsonCodecPayload("player-1").Encode();

        Assert.Equal("""{"accessToken":"player-1"}""", message.GetString());
    }

    private sealed record JsonCodecPayload(string AccessToken);
}

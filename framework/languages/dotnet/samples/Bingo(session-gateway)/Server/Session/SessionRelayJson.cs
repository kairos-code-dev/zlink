using System.Text.Json;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;

namespace Bingo.SessionGateway.Session;

internal static class SessionRelayJson
{
    private static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web);

    public static T Decode<T>(Message payload)
    {
        return payload.FromJson<T>(Options);
    }
}

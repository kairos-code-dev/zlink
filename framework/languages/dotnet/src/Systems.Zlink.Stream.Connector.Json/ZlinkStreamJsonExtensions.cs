using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Systems.Zlink.Stream.Connector.Json;

public static class ZlinkStreamJsonCodec
{
    private static JsonSerializerOptions _serializerOptions = new(JsonSerializerDefaults.Web);

    public static JsonSerializerOptions SerializerOptions => _serializerOptions;

    public static void Configure(JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        _serializerOptions = options;
    }
}

public static class ZlinkStreamJsonExtensions
{
    public static T FromJson<T>(this ZlinkStreamEncodedPayload payload)
    {
        EnsureJson(payload);
        return JsonSerializer.Deserialize<T>(payload.Payload.Span, ZlinkStreamJsonCodec.SerializerOptions)!;
    }

    public static ZlinkStreamEncodedPayload ToJson<T>(this T value)
    {
        return new ZlinkStreamEncodedPayload(
            ZlinkStreamCodec.Json,
            JsonSerializer.SerializeToUtf8Bytes(value, ZlinkStreamJsonCodec.SerializerOptions),
            typeof(T));
    }

    private static void EnsureJson(ZlinkStreamEncodedPayload payload)
    {
        if (payload.Codec != ZlinkStreamCodec.Json)
        {
            throw new InvalidOperationException($"Stream payload codec is {payload.Codec}, not Json.");
        }
    }
}

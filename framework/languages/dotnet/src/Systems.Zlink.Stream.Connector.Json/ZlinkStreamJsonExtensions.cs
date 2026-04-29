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
    public static T FromJson<T>(this ZlinkStreamEncodedBody body)
    {
        EnsureJson(body);
        return JsonSerializer.Deserialize<T>(body.Body.Span, ZlinkStreamJsonCodec.SerializerOptions)!;
    }

    public static ZlinkStreamEncodedBody ToJson<T>(this T value)
    {
        return new ZlinkStreamEncodedBody(
            ZlinkStreamCodec.Json,
            JsonSerializer.SerializeToUtf8Bytes(value, ZlinkStreamJsonCodec.SerializerOptions),
            typeof(T));
    }

    private static void EnsureJson(ZlinkStreamEncodedBody body)
    {
        if (body.Codec != ZlinkStreamCodec.Json)
        {
            throw new InvalidOperationException($"Stream body codec is {body.Codec}, not Json.");
        }
    }
}

// SPDX-License-Identifier: MPL-2.0

using System.Text.Json;
using Zlink;

namespace Zlink.Codecs.Json;

public static class ZlinkJsonCodec
{
    private static JsonSerializerOptions _serializerOptions = new(JsonSerializerDefaults.Web);

    public static JsonSerializerOptions SerializerOptions => _serializerOptions;

    public static void Configure(JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        _serializerOptions = options;
    }
}

public static class JsonMessageExtensions
{
    public static T FromJson<T>(this Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        return JsonSerializer.Deserialize<T>(message.AsReadOnlySpan(), ZlinkJsonCodec.SerializerOptions)!;
    }

    public static Message ToJson<T>(this T value)
    {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(value, ZlinkJsonCodec.SerializerOptions);
        return Message.FromOwnedBytes(payload);
    }
}

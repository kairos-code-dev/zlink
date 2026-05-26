// SPDX-License-Identifier: MPL-2.0

using System.Text.Json;
using Systems.Zlink;

namespace Systems.Zlink.Codecs.Json;

public static class JsonMessageExtensions
{
    private static readonly JsonSerializerOptions DefaultOptions =
        new(JsonSerializerDefaults.Web);

    public static T Decode<T>(this Message message,
        JsonSerializerOptions? options = null)
    {
        return message.FromJson<T>(options ?? DefaultOptions);
    }

    public static T FromJson<T>(this Message message,
        JsonSerializerOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(message);
        return JsonSerializer.Deserialize<T>(message.AsReadOnlySpan(),
            options)!;
    }

    public static Message Encode<T>(this T value,
        JsonSerializerOptions? options = null)
    {
        return value.ToJson(options ?? DefaultOptions);
    }

    public static Message ToJson<T>(this T value,
        JsonSerializerOptions? options = null)
    {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(value, options);
        return Message.From(payload);
    }
}

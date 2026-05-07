// SPDX-License-Identifier: MPL-2.0

using System.Text.Json;
using Systems.Zlink;

namespace Systems.Zlink.Codecs.Json;

public static class JsonMessageExtensions
{
    public static T FromJson<T>(this Message message,
        JsonSerializerOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(message);
        return JsonSerializer.Deserialize<T>(message.AsReadOnlySpan(),
            options)!;
    }

    public static Message ToJson<T>(this T value,
        JsonSerializerOptions? options = null)
    {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(value, options);
        return Message.FromBytes(payload);
    }
}

// SPDX-License-Identifier: MPL-2.0

using System.Text.Json;
using Zlink;

namespace Zlink.Codecs.Json;

public static class JsonCodec
{
    public static T Decode<T>(this Message message, JsonSerializerOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(message);
        return JsonSerializer.Deserialize<T>(message.AsReadOnlySpan(), options)!;
    }

    public static Message Encode<T>(this T value, JsonSerializerOptions? options = null)
    {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(value, options);
        return Message.FromBytes(payload);
    }
}

// SPDX-License-Identifier: MPL-2.0

using System;
using System.Text.Json;
using Zlink;

namespace Zlink.Codecs.Json;

public static class JsonMessageExtensions
{
    public static T ParseJson<T>(this Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        return JsonSerializer.Deserialize<T>(message.AsReadOnlySpan())!;
    }

    public static Message ToMessage<T>(this T value)
    {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(value);
        return Message.FromOwnedBytes(payload);
    }

    public static Message ToJsonMessage<T>(this T value)
    {
        return value.ToMessage();
    }
}

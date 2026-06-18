using System.Text.Json;
using Systems.Zlink;

namespace Zlink.Framework.Contracts.Codecs.Json;

public static class JsonMessageExtensions
{
    private static readonly JsonSerializerOptions DefaultOptions =
        new(JsonSerializerDefaults.Web);

    public static T Decode<T>(
        this Message message,
        JsonSerializerOptions? options = null)
    {
        return message.FromJson<T>(options ?? DefaultOptions);
    }

    public static T FromJson<T>(
        this Message message,
        JsonSerializerOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(message);
        return JsonSerializer.Deserialize<T>(
            message.AsReadOnlySpan(),
            options ?? DefaultOptions)!;
    }

    public static Message Encode<T>(
        this T value,
        JsonSerializerOptions? options = null)
    {
        return value.ToJson(options ?? DefaultOptions);
    }

    public static Message ToJson<T>(
        this T value,
        JsonSerializerOptions? options = null)
    {
        return Message.From(JsonSerializer.SerializeToUtf8Bytes(
            value,
            options ?? DefaultOptions));
    }
}

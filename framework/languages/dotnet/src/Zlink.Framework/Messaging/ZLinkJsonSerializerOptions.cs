using System.Text.Json;

namespace Zlink.Framework.Messaging;

internal static class ZLinkJsonSerializerOptions
{
    public static JsonSerializerOptions Default { get; } = new(JsonSerializerDefaults.Web);
}

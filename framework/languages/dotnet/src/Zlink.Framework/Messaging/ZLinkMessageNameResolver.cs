using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Messaging;

internal static class ZLinkMessageNameResolver
{
    private static readonly ConcurrentDictionary<Type, string> Cache = new();

    public static string ResolveFromMessage(object? message)
    {
        var messageType = message?.GetType()
            ?? throw new InvalidOperationException("Message type is required.");
        return ResolveFromType(messageType);
    }

    public static string ResolveFromType(Type type)
    {
        ArgumentNullException.ThrowIfNull(type);
        return Cache.GetOrAdd(type, static messageType =>
            messageType.GetCustomAttribute<ZLinkPacketAttribute>()?.PacketName
            ?? messageType.Name);
    }
}

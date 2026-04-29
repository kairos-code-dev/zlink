using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Messaging;

internal static class ZLinkMessageNameResolver
{
    public static string ResolveFromMessage(object? message)
    {
        var messageType = message?.GetType()
            ?? throw new InvalidOperationException("Message type is required.");
        return ResolveFromType(messageType);
    }

    public static string ResolveFromType(Type type)
    {
        return type.GetCustomAttribute<ZLinkPacketAttribute>()?.PacketName
            ?? type.Name;
    }
}

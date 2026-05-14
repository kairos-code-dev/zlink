using System.Collections.Concurrent;
using System.Reflection;

namespace Systems.Zlink.Stream.Connector.Runtime.Protocol;

internal sealed class ZlinkStreamPacketNameResolver : IZlinkStreamPacketNameResolver
{
    private static readonly ConcurrentDictionary<Type, string> Cache = new();

    public string Resolve(Type bodyType)
    {
        ArgumentNullException.ThrowIfNull(bodyType);
        return Cache.GetOrAdd(bodyType, static type =>
            type.GetCustomAttribute<ZlinkStreamPacketNameAttribute>(inherit: false)?.Name
            ?? type.Name);
    }
}

namespace Systems.Zlink.Stream.Connector.Protocol;

public sealed class ZlinkStreamPacketNameResolver : IZlinkStreamPacketNameResolver
{
    public string Resolve(Type bodyType)
    {
        ArgumentNullException.ThrowIfNull(bodyType);
        var attribute = bodyType.GetCustomAttributes(typeof(ZlinkStreamPacketNameAttribute), false)
            .OfType<ZlinkStreamPacketNameAttribute>()
            .FirstOrDefault();
        return attribute?.Name ?? bodyType.Name;
    }
}

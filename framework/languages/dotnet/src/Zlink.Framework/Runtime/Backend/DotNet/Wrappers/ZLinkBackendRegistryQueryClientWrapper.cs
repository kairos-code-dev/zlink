namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendRegistryQueryClientWrapper(IRegistryQueryClient nativeClient)
    : IZLinkBackendRegistryQueryClient
{
    public object NativeInstance => nativeClient;

    public void Connect(string endpoint)
    {
        nativeClient.Connect(endpoint);
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> Topology(
        ZLinkRegistryTopologyFilter? filter)
    {
        return nativeClient.Topology(filter.ToNative())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public ValueTask DisposeAsync()
    {
        return nativeClient.DisposeAsync();
    }
}
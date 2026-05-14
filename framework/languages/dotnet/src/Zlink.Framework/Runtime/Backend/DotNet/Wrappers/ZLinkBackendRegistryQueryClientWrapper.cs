namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendRegistryQueryClientWrapper(RegistryQueryClient nativeClient)
    : IZLinkBackendRegistryQueryClient
{
    public object NativeInstance => nativeClient;

    public void Connect(string endpoint)
    {
        nativeClient.Connect(endpoint);
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> Snapshot(
        ZLinkRegistryTopologyFilter? filter)
    {
        return nativeClient.Snapshot(filter.ToNative())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public ValueTask DisposeAsync() => nativeClient.DisposeAsync();
}

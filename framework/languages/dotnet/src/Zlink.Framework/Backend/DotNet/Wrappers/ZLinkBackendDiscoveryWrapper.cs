namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendDiscoveryWrapper(global::Zlink.Discovery nativeDiscovery) : IZLinkBackendDiscovery
{
    public object NativeInstance => nativeDiscovery;

    public void ConnectRegistry(string endpoint)
    {
        nativeDiscovery.ConnectRegistry(endpoint);
    }

    public ValueTask DisposeAsync() => nativeDiscovery.DisposeAsync();
}

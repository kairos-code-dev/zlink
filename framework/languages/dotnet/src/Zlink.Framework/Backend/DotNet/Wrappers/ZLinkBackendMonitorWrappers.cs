namespace Zlink.Framework.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendSocketMonitorWrapper(SocketMonitor nativeMonitor) : IZLinkBackendSocketMonitor
{
    public object NativeInstance => nativeMonitor;

    public void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler)
    {
        nativeMonitor.OnEvent(monitorEvent => handler(monitorEvent.ToFramework()));
    }

    public ZLinkBackendSocketMonitorEvent Recv()
    {
        return nativeMonitor.Recv().ToFramework();
    }

    public ValueTask DisposeAsync() => nativeMonitor.DisposeAsync();
}

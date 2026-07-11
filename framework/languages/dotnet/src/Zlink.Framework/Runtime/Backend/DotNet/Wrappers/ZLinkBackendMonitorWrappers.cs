namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendSocketMonitorWrapper(ISocketMonitor nativeMonitor) : IZLinkBackendSocketMonitor
{
    public void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler)
    {
        nativeMonitor.OnEvent(monitorEvent => handler(monitorEvent.ToFramework()));
    }

    public ZLinkBackendSocketMonitorEvent Recv()
    {
        return (nativeMonitor.Recv(RecvFlags.DontWait)
                ?? throw new ZlinkRecvException(ZlinkRecvException.ErrorCode.NoData))
            .ToFramework();
    }

    public ValueTask DisposeAsync()
    {
        return nativeMonitor.DisposeAsync();
    }
}

namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendSocketMonitorWrapper(ISocketMonitor nativeMonitor) : IZLinkBackendSocketMonitor
{
    public void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler)
    {
        nativeMonitor.OnEvent(monitorEvent => handler(monitorEvent.ToFramework()));
    }

    public bool TryRecv(out ZLinkBackendSocketMonitorEvent monitorEvent)
    {
        var nativeEvent = nativeMonitor.Recv(RecvFlags.DontWait);
        if (nativeEvent is null)
        {
            monitorEvent = default;
            return false;
        }

        monitorEvent = nativeEvent.ToFramework();
        return true;
    }

    public ValueTask DisposeAsync()
    {
        return nativeMonitor.DisposeAsync();
    }
}

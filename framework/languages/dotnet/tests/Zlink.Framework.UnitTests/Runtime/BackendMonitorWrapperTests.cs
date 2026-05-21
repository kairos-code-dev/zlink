namespace Zlink.Framework.UnitTests;

public sealed class BackendMonitorWrapperTests
{
    [Fact]
    public void BackendMonitorWrapper_Uses_NonBlocking_Receive()
    {
        var monitor = new RecordingSocketMonitor(null);
        var wrapper = new ZLinkBackendSocketMonitorWrapper(monitor);

        var error = Assert.Throws<ZlinkRecvException>(() => wrapper.Recv());

        Assert.Equal(ZlinkRecvException.ErrorCode.NoData, error.Result);
        Assert.Equal(RecvFlags.DontWait, monitor.LastRecvFlags);
    }

    private sealed class RecordingSocketMonitor(MonitorEvent? nextEvent) : ISocketMonitor
    {
        public RecvFlags? LastRecvFlags { get; private set; }

        public void OnEvent(Action<MonitorEvent> handler)
        {
        }

        public MonitorEvent? Recv(RecvFlags flags = RecvFlags.None)
        {
            LastRecvFlags = flags;
            return nextEvent;
        }

        public MonitorSnapshot Snapshot()
        {
            throw new NotSupportedException();
        }

        public void Close()
        {
        }

        public void Dispose()
        {
        }

        public ValueTask DisposeAsync()
        {
            return ValueTask.CompletedTask;
        }
    }
}

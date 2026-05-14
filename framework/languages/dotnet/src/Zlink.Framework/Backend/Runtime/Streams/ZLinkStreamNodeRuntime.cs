using Zlink.Framework.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamNodeRuntime : IAsyncDisposable
{
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly ZLinkStreamSessionTable _sessions;
    private Task? _monitorLoop;

    public ZLinkStreamNodeRuntime(
        string nodeName,
        IServiceProvider services,
        IZLinkBackendStreamSocket socket,
        IZLinkBackendSocketMonitor monitor,
        Type? headerSessionType,
        ZLinkRuntimeTaskRunner taskRunner)
    {
        NodeName = nodeName;
        Socket = socket;
        Monitor = monitor;
        _taskRunner = taskRunner;
        _sessions = new ZLinkStreamSessionTable(services, socket, headerSessionType);
    }

    public string NodeName { get; }

    public IZLinkBackendStreamSocket Socket { get; }

    public IZLinkBackendSocketMonitor Monitor { get; }

    public void Start()
    {
        RegisterWithoutSynchronizationContext(() =>
        {
            Socket.OnFramedPacket(OnFramedPacket);
            return 0;
        });

        _monitorLoop = _taskRunner.Run(
            $"stream-monitor:{NodeName}",
            _ => new ValueTask(RunMonitorLoopAsync(_stopSource.Token)));
    }

    public async ValueTask DisposeAsync()
    {
        var sessions = _sessions.Stop();

        _stopSource.Cancel();
        await Monitor.DisposeAsync();

        if (_monitorLoop is not null)
        {
            try
            {
                await _monitorLoop;
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }

        foreach (var session in sessions)
        {
            await session.DisposeAsync();
        }

        await Socket.DisposeAsync();
        _stopSource.Dispose();
    }

    private void OnFramedPacket(
        string routingIdText,
        Message header,
        Message body)
    {
        var routingId = ParsePublicRoutingId(routingIdText);
        if (!_sessions.TryGetOrCreate(routingId, out var session))
        {
            header.Dispose();
            body.Dispose();
            return;
        }

        _sessions.ApplyPendingConnectionMetadata(session);
        session.EnqueuePacket(header, body);
    }

    private void OnMonitorEvent(ZLinkBackendSocketMonitorEvent monitorEvent)
    {
        if (_sessions.IsStopping)
        {
            return;
        }

        switch (monitorEvent.NativeEvent)
        {
            case ZLinkSocketNativeEventType.ConnectionReady:
                if (monitorEvent.RoutingId is RoutingId readyRoutingId)
                {
                    if (_sessions.TryGetOrCreate(readyRoutingId, out var session))
                    {
                        session.EnqueueConnected(monitorEvent.LocalAddr, monitorEvent.RemoteAddr);
                    }
                }
                else
                {
                    _sessions.QueueConnectionMetadata(monitorEvent.LocalAddr, monitorEvent.RemoteAddr);
                }
                break;
            case ZLinkSocketNativeEventType.Accepted:
                break;
            case ZLinkSocketNativeEventType.Disconnected:
                if (_sessions.TryResolveMonitorSession(monitorEvent.RoutingId, out var disconnectedSession))
                {
                    disconnectedSession.EnqueueDisconnected(
                        new ZLinkStreamError(
                            ZLinkStreamSessionError.TransportError,
                            new ZLinkStreamDiagnostic((int)monitorEvent.Value, monitorEvent.NativeEvent.ToString())));
                }
                break;
        }
    }

    private async Task RunMonitorLoopAsync(CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var monitorEvent = Monitor.Recv();
                backoff.Reset();
                OnMonitorEvent(monitorEvent);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkRecvException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.Result is ZlinkRecvException.ErrorCode.InternalError
                      or ZlinkRecvException.ErrorCode.InvalidHandle)
            {
                return;
            }
            catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                continue;
            }

            await Task.Yield();
        }
    }

    private static RoutingId ParsePublicRoutingId(string routingIdText)
    {
        if (routingIdText.StartsWith("hex:", StringComparison.OrdinalIgnoreCase))
        {
            return RoutingId.FromBytes(
                Convert.FromHexString(routingIdText["hex:".Length..]));
        }

        return RoutingId.FromBytes(
            System.Text.Encoding.UTF8.GetBytes(routingIdText));
    }

    private static T RegisterWithoutSynchronizationContext<T>(Func<T> action)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            return action();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }
}

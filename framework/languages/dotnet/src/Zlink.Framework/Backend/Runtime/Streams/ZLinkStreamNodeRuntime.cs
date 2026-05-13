using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;
using System.Threading;
using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamNodeRuntime : IAsyncDisposable
{
    private readonly IServiceProvider _services;
    private readonly Type? _headerSessionType;
    private readonly Dictionary<string, ZLinkStreamSessionRuntime> _sessions = [];
    private readonly Queue<(string LocalAddr, string RemoteAddr)> _pendingConnectionMetadata = [];
    private readonly object _gate = new();
    private readonly CancellationTokenSource _stopSource = new();
    private Task? _monitorLoop;
    private bool _stopping;

    public ZLinkStreamNodeRuntime(
        string nodeName,
        IServiceProvider services,
        IZLinkBackendStreamSocket socket,
        IZLinkBackendSocketMonitor monitor,
        Type? headerSessionType)
    {
        NodeName = nodeName;
        _services = services;
        Socket = socket;
        Monitor = monitor;
        _headerSessionType = headerSessionType;
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

        _monitorLoop = Task.Run(
            () => RunMonitorLoopAsync(_stopSource.Token),
            CancellationToken.None);
    }

    public async ValueTask DisposeAsync()
    {
        ZLinkStreamSessionRuntime[] sessions;
        lock (_gate)
        {
            _stopping = true;
            sessions = _sessions.Values.ToArray();
            _sessions.Clear();
            _pendingConnectionMetadata.Clear();
        }

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
        if (!TryGetOrCreateSession(routingId, out var session))
        {
            header.Dispose();
            body.Dispose();
            return;
        }

        ApplyPendingConnectionMetadata(session);
        session.EnqueuePacket(header, body);
    }

    private void OnMonitorEvent(ZLinkBackendSocketMonitorEvent monitorEvent)
    {
        lock (_gate)
        {
            if (_stopping)
            {
                return;
            }
        }

        switch (monitorEvent.NativeEvent)
        {
            case ZLinkSocketNativeEventType.ConnectionReady:
                if (monitorEvent.RoutingId is RoutingId readyRoutingId)
                {
                    if (TryGetOrCreateSession(readyRoutingId, out var session))
                    {
                        session.EnqueueConnected(monitorEvent.LocalAddr, monitorEvent.RemoteAddr);
                    }
                }
                else
                {
                    lock (_gate)
                    {
                        _pendingConnectionMetadata.Enqueue((monitorEvent.LocalAddr, monitorEvent.RemoteAddr));
                    }
                }
                break;
            case ZLinkSocketNativeEventType.Accepted:
                break;
            case ZLinkSocketNativeEventType.Disconnected:
                if (TryResolveSessionForMonitorEvent(monitorEvent.RoutingId, out var disconnectedSession))
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
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var monitorEvent = Monitor.Recv();
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
                await ZLinkPollingBackoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                continue;
            }

            await Task.Yield();
        }
    }

    private bool TryGetOrCreateSession(
        RoutingId routingId,
        out ZLinkStreamSessionRuntime session)
    {
        var sessionId = routingId.ToHex();
        lock (_gate)
        {
            if (_stopping)
            {
                session = null!;
                return false;
            }

            if (_sessions.TryGetValue(sessionId, out var existing))
            {
                session = existing;
                return true;
            }

            var created = new ZLinkStreamSessionRuntime(
                _services.CreateAsyncScope(),
                Socket,
                routingId,
                _headerSessionType,
                RemoveSession);
            _sessions.Add(sessionId, created);
            session = created;
            return true;
        }
    }

    private void RemoveSession(RoutingId routingId)
    {
        RemoveSession(routingId.ToHex());
    }

    private void RemoveSession(string sessionId)
    {
        lock (_gate)
        {
            _sessions.Remove(sessionId);
        }
    }

    private void ApplyPendingConnectionMetadata(ZLinkStreamSessionRuntime session)
    {
        (string LocalAddr, string RemoteAddr)? metadata = null;

        lock (_gate)
        {
            if (session.Stream.LocalAddr is null
                && session.Stream.RemoteAddr is null
                && _pendingConnectionMetadata.Count > 0)
            {
                metadata = _pendingConnectionMetadata.Dequeue();
            }
        }

        if (metadata is { } value)
        {
            session.EnqueueConnected(value.LocalAddr, value.RemoteAddr);
        }
    }

    private bool TryResolveSessionForMonitorEvent(
        RoutingId? routingId,
        out ZLinkStreamSessionRuntime session)
    {
        lock (_gate)
        {
            if (routingId is RoutingId streamRoutingId)
            {
                var sessionId = streamRoutingId.ToHex();
                if (_sessions.TryGetValue(sessionId, out var existing))
                {
                    session = existing;
                    return true;
                }
            }

            if (_sessions.Count == 1)
            {
                session = _sessions.Values.First();
                return true;
            }
        }

        session = null!;
        return false;
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

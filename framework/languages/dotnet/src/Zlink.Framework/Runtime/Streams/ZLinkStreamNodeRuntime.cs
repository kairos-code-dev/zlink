using System.Text;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamNodeRuntime : IAsyncDisposable
{
    private readonly ZLinkStreamSessionTable _sessions;
    private readonly ZLinkStreamSessionSerialExecutor _sessionIngress;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly string _transport;
    private Task? _monitorLoop;

    public ZLinkStreamNodeRuntime(
        string nodeName,
        IServiceProvider services,
        IZLinkBackendStreamSocket socket,
        IZLinkBackendSocketMonitor monitor,
        Type? headerSessionType,
        ZLinkRuntimeTaskRunner taskRunner,
        string transport)
    {
        NodeName = nodeName;
        Socket = socket;
        Monitor = monitor;
        _taskRunner = taskRunner;
        _transport = transport;
        var runtime = services.GetRequiredService<ZLinkFrameworkRuntime>();
        _sessionIngress = new ZLinkStreamSessionSerialExecutor(runtime.ExecutionOwner);
        _sessions = new ZLinkStreamSessionTable(
            services,
            socket,
            headerSessionType,
            runtime.DrainAdmission,
            transport);
    }

    public string NodeName { get; }

    public IZLinkBackendStreamSocket Socket { get; }

    public IZLinkBackendSocketMonitor Monitor { get; }

    internal void RequestStop()
    {
        _stopSource.Cancel();
        _sessionIngress.RequestStop();
        _sessions.RequestStop();
    }

    internal ValueTask<bool> DrainSessionsAsync(CancellationToken cancellationToken) =>
        _sessions.DrainSessionsAsync(cancellationToken);

    public async ValueTask DisposeAsync()
    {
        var sessions = _sessions.Stop();
        var failures = new List<Exception>();
        Capture(RequestStop);
        await CaptureAsync(_sessionIngress.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(Monitor.DisposeAsync).ConfigureAwait(false);

        if (_monitorLoop is not null)
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
            catch (Exception exception)
            {
                failures.Add(exception);
            }

        foreach (var session in sessions)
            await CaptureAsync(session.DisposeAsync).ConfigureAwait(false);

        await CaptureAsync(Socket.DisposeAsync).ConfigureAwait(false);
        Capture(_stopSource.Dispose);
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        void Capture(Action cleanup)
        {
            try
            {
                cleanup();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    public void Start()
    {
        RegisterWithoutSynchronizationContext(() =>
        {
            Socket.OnFramedPacket(OnFramedPacket);
            return 0;
        });

        _monitorLoop = _taskRunner.Run(
            $"stream-monitor:{NodeName}",
            runtimeToken => new ValueTask(RunMonitorLoopUntilStoppedAsync(runtimeToken)));
    }

    private async Task RunMonitorLoopUntilStoppedAsync(CancellationToken runtimeToken)
    {
        using var stop = CancellationTokenSource.CreateLinkedTokenSource(
            _stopSource.Token,
            runtimeToken);
        await RunMonitorLoopAsync(stop.Token).ConfigureAwait(false);
    }

    private void OnFramedPacket(
        string routingIdText,
        Message header,
        Message payload)
    {
        ZLinkRuntimeMetrics.RecordStreamBytes(
            inbound: true,
            ZLinkStreamFrameCodec.PrefixSize
            + header.AsReadOnlyMemory().Length
            + payload.AsReadOnlyMemory().Length,
            _transport);
        if (_sessionIngress.Enqueue(async () =>
        {
            var ownershipTransferred = false;
            try
            {
                var routingId = ParsePublicRoutingId(routingIdText);
                var session = await _sessions.GetOrCreateAsync(routingId).ConfigureAwait(false);
                if (session is null) return;

                _sessions.ApplyPendingConnectionMetadata(session);
                session.EnqueuePacket(header, payload);
                ownershipTransferred = true;
            }
            finally
            {
                if (!ownershipTransferred)
                {
                    header.Dispose();
                    payload.Dispose();
                }
            }
        })) return;

        header.Dispose();
        payload.Dispose();
    }

    private void OnMonitorEvent(ZLinkBackendSocketMonitorEvent monitorEvent)
    {
        if (_sessions.IsStopping) return;

        switch (monitorEvent.NativeEvent)
        {
            case ZLinkSocketNativeEventType.ConnectionReady:
                if (monitorEvent.RoutingId is RoutingId readyRoutingId)
                {
                    _ = _sessionIngress.Enqueue(async () =>
                    {
                        var session = await _sessions.GetOrCreateAsync(readyRoutingId).ConfigureAwait(false);
                        session?.EnqueueConnected(monitorEvent.LocalAddr, monitorEvent.RemoteAddr);
                    });
                }
                else
                {
                    _ = _sessionIngress.Enqueue(() =>
                    {
                        _sessions.QueueConnectionMetadata(
                            monitorEvent.LocalAddr,
                            monitorEvent.RemoteAddr);
                        return ValueTask.CompletedTask;
                    });
                }

                break;
            case ZLinkSocketNativeEventType.Accepted:
                break;
            case ZLinkSocketNativeEventType.Disconnected:
                _ = _sessionIngress.Enqueue(() =>
                {
                    if (_sessions.TryResolveMonitorSession(monitorEvent.RoutingId, out var disconnectedSession))
                        disconnectedSession.EnqueueDisconnected(
                            new ZLinkStreamError(
                                ZLinkStreamSessionError.TransportError,
                                new ZLinkStreamDiagnostic(
                                    (int)monitorEvent.Value,
                                    monitorEvent.NativeEvent.ToString())));
                    return ValueTask.CompletedTask;
                });
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
            return RoutingId.From(
                Convert.FromHexString(routingIdText["hex:".Length..]));

        return RoutingId.From(
            Encoding.UTF8.GetBytes(routingIdText));
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

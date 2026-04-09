// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Zlink;

public delegate void RequestReplyCallback(Received reply);
public delegate void RequestErrorCallback(ZlinkException error);

internal sealed class PendingRequest
{
    public TaskCompletionSource<Received> Completion { get; } =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public bool TrySetResult(Received received)
    {
        return Completion.TrySetResult(received);
    }

    public bool TrySetException(Exception ex)
    {
        return Completion.TrySetException(ex);
    }

    public bool TrySetCanceled(CancellationToken token)
    {
        return Completion.TrySetCanceled(token);
    }
}

public sealed class RequestDealer : IDisposable, IAsyncDisposable
{
    private static readonly TimeSpan DefaultTimeoutValue = TimeSpan.FromSeconds(30);
    private static readonly TimeSpan PollDelay = TimeSpan.FromMilliseconds(1);

    private readonly DealerSocket _socket;
    private readonly ConcurrentDictionary<ulong, PendingRequest> _pending = new();
    private readonly BlockingCollection<Received> _dataQueue = new();
    private readonly CancellationTokenSource _stop = new();
    private readonly Thread _dispatchThread;
    private long _nextCorrelationId = 1;
    private SocketRecvHandler? _dataHandler;
    private SynchronizationContext? _dataHandlerContext;
    private int _closed;

    public RequestDealer(DealerSocket socket)
    {
        _socket = socket ?? throw new ArgumentNullException(nameof(socket));
        _dispatchThread = new Thread(DispatchLoop)
        {
            IsBackground = true,
            Name = "Zlink.RequestDealer"
        };
        _dispatchThread.Start();
    }

    public DealerSocket Socket => _socket;

    public TimeSpan DefaultRequestTimeout { get; set; } = DefaultTimeoutValue;

    public Task<Received> RequestAsync(Message message,
        CancellationToken ct = default)
        => RequestAsync(new[] { message }, DefaultRequestTimeout, ct);

    public Task<Received> RequestAsync(Message message, TimeSpan timeout,
        CancellationToken ct = default)
        => RequestAsync(new[] { message }, timeout, ct);

    public Task<Received> RequestAsync(IReadOnlyList<Message> parts,
        CancellationToken ct = default)
        => RequestAsync(parts, DefaultRequestTimeout, ct);

    public Task<Received> RequestAsync(IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct = default)
        => RequestCoreAsync(parts, timeout, ct, trySend: false);

    public Task<Received> TryRequestAsync(Message message,
        CancellationToken ct = default)
        => TryRequestAsync(new[] { message }, DefaultRequestTimeout, ct);

    public Task<Received> TryRequestAsync(Message message, TimeSpan timeout,
        CancellationToken ct = default)
        => TryRequestAsync(new[] { message }, timeout, ct);

    public Task<Received> TryRequestAsync(IReadOnlyList<Message> parts,
        CancellationToken ct = default)
        => TryRequestAsync(parts, DefaultRequestTimeout, ct);

    public Task<Received> TryRequestAsync(IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct = default)
        => RequestCoreAsync(parts, timeout, ct, trySend: true);

    public void Request(Message message, RequestReplyCallback onReply,
        RequestErrorCallback onError)
        => Request(new[] { message }, onReply, onError, DefaultRequestTimeout);

    public void Request(Message message, RequestReplyCallback onReply,
        RequestErrorCallback onError, TimeSpan timeout)
        => Request(new[] { message }, onReply, onError, timeout);

    public void Request(IReadOnlyList<Message> parts, RequestReplyCallback onReply,
        RequestErrorCallback onError)
        => Request(parts, onReply, onError, DefaultRequestTimeout);

    public void Request(IReadOnlyList<Message> parts, RequestReplyCallback onReply,
        RequestErrorCallback onError, TimeSpan timeout)
        => RequestReplySupport.AttachCallback(() => RequestAsync(parts, timeout), onReply, onError);

    public void TryRequest(Message message, RequestReplyCallback onReply,
        RequestErrorCallback onError)
        => TryRequest(new[] { message }, onReply, onError, DefaultRequestTimeout);

    public void TryRequest(Message message, RequestReplyCallback onReply,
        RequestErrorCallback onError, TimeSpan timeout)
        => TryRequest(new[] { message }, onReply, onError, timeout);

    public void TryRequest(IReadOnlyList<Message> parts, RequestReplyCallback onReply,
        RequestErrorCallback onError)
        => TryRequest(parts, onReply, onError, DefaultRequestTimeout);

    public void TryRequest(IReadOnlyList<Message> parts, RequestReplyCallback onReply,
        RequestErrorCallback onError, TimeSpan timeout)
        => RequestReplySupport.AttachCallback(() => TryRequestAsync(parts, timeout), onReply, onError);

    public Received Recv()
    {
        if (_dataHandler != null)
            throw new InvalidOperationException("socket is in callback mode");
        try
        {
            return _dataQueue.Take(_stop.Token);
        }
        catch (OperationCanceledException)
        {
            throw new ZlinkException((int)ErrorCode.Eterm, "socket is closed");
        }
        catch (InvalidOperationException)
        {
            throw new ZlinkException((int)ErrorCode.Eterm, "socket is closed");
        }
    }

    public bool TryRecv(out Received? received)
    {
        if (_dataHandler != null)
            throw new InvalidOperationException("socket is in callback mode");
        return _dataQueue.TryTake(out received, 0);
    }

    public void OnReceive(SocketRecvHandler handler)
    {
        _dataHandler = handler ?? throw new ArgumentNullException(nameof(handler));
        _dataHandlerContext = SynchronizationContext.Current;
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        _stop.Cancel();
        _socket.Dispose();
        _dispatchThread.Join(TimeSpan.FromSeconds(1));
        _dataQueue.CompleteAdding();
        FailAll(new ZlinkException((int)ErrorCode.Eterm, "socket is closed"));
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    private Task<Received> RequestCoreAsync(IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, bool trySend)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        ulong correlationId = unchecked((ulong)Interlocked.Increment(ref _nextCorrelationId) - 1);
        cloned[0].SetRequest(correlationId);
        var pending = new PendingRequest();
        if (!_pending.TryAdd(correlationId, pending))
        {
            RequestReplySupport.DisposeParts(cloned);
            throw new InvalidOperationException("duplicate correlation id");
        }

        CancellationTokenRegistration ctr = default;
        Timer? timeoutTimer = null;
        if (ct.CanBeCanceled)
        {
            ctr = ct.Register(() =>
            {
                if (_pending.TryRemove(correlationId, out PendingRequest? removed))
                    removed.TrySetCanceled(ct);
            });
        }
        if (timeout <= TimeSpan.Zero)
            timeout = DefaultRequestTimeout;
        timeoutTimer = new Timer(_ =>
        {
            if (_pending.TryRemove(correlationId, out PendingRequest? removed))
            {
                removed.TrySetException(new ZlinkException((int)ErrorCode.ETimedOut,
                    "request timed out"));
            }
        }, null, timeout, Timeout.InfiniteTimeSpan);

        try
        {
            if (trySend)
            {
                SendResult result = _socket.TrySend(cloned);
                if (result != SendResult.Sent)
                    throw new ZlinkException((int)ErrorCode.EAgain,
                        "request send is backpressured");
            }
            else
            {
                _socket.Send(cloned);
            }
        }
        catch (Exception ex)
        {
            _pending.TryRemove(correlationId, out _);
            timeoutTimer.Dispose();
            ctr.Dispose();
            RequestReplySupport.DisposeParts(cloned);
            return Task.FromException<Received>(ex);
        }

        pending.Completion.Task.ContinueWith(_ =>
        {
            timeoutTimer.Dispose();
            ctr.Dispose();
        }, TaskScheduler.Default);
        return pending.Completion.Task;
    }

    private void DispatchLoop()
    {
        while (!_stop.IsCancellationRequested)
        {
            try
            {
                if (!_socket.TryRecv(out Received? received))
                {
                    Thread.Sleep(PollDelay);
                    continue;
                }
                if (received == null)
                    continue;
                RouteReceived(RequestReplySupport.CloneReceived(received));
            }
            catch (ZlinkException ex) when (ex.Errno == (int)ErrorCode.Eterm)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            catch (Exception ex)
            {
                FailAll(ex);
                break;
            }
        }
    }

    private void RouteReceived(Received received)
    {
        if (received.Parts.Count == 0)
        {
            DeliverData(received);
            return;
        }
        (byte msgType, ulong correlationId) = received.Parts[0].GetRequestInfo();
        if (msgType == 2 && _pending.TryRemove(correlationId, out PendingRequest? pending))
        {
            pending.TrySetResult(received);
            return;
        }
        DeliverData(received);
    }

    private void DeliverData(Received received)
    {
        SocketRecvHandler? handler = _dataHandler;
        if (handler == null)
        {
            if (!_dataQueue.IsAddingCompleted)
                _dataQueue.Add(received);
            return;
        }
        SynchronizationContext? context = _dataHandlerContext;
        CallbackDelivery.Post(context, () => handler(received.RoutingId,
            RequestReplySupport.ToArray(received.Parts)));
    }

    private void FailAll(Exception ex)
    {
        foreach ((ulong key, PendingRequest pending) in _pending)
        {
            if (_pending.TryRemove(key, out PendingRequest? removed))
                removed.TrySetException(ex);
        }
    }

}

public sealed class RequestRouter : IDisposable, IAsyncDisposable
{
    private static readonly TimeSpan DefaultTimeoutValue = TimeSpan.FromSeconds(30);
    private static readonly TimeSpan PollDelay = TimeSpan.FromMilliseconds(1);

    private readonly RouterSocket _socket;
    private readonly ConcurrentDictionary<ulong, PendingRequest> _pending = new();
    private readonly BlockingCollection<Received> _dataQueue = new();
    private readonly CancellationTokenSource _stop = new();
    private readonly Thread _dispatchThread;
    private long _nextCorrelationId = 1;
    private SocketRecvHandler? _dataHandler;
    private SynchronizationContext? _dataHandlerContext;
    private int _closed;

    public RequestRouter(RouterSocket socket)
    {
        _socket = socket ?? throw new ArgumentNullException(nameof(socket));
        _dispatchThread = new Thread(DispatchLoop)
        {
            IsBackground = true,
            Name = "Zlink.RequestRouter"
        };
        _dispatchThread.Start();
    }

    public RouterSocket Socket => _socket;

    public TimeSpan DefaultRequestTimeout { get; set; } = DefaultTimeoutValue;

    public Task<Received> RequestAsync(RoutingId routingId, Message message,
        CancellationToken ct = default)
        => RequestAsync(routingId, new[] { message }, DefaultRequestTimeout, ct);

    public Task<Received> RequestAsync(RoutingId routingId, Message message,
        TimeSpan timeout, CancellationToken ct = default)
        => RequestAsync(routingId, new[] { message }, timeout, ct);

    public Task<Received> RequestAsync(RoutingId routingId, IReadOnlyList<Message> parts,
        CancellationToken ct = default)
        => RequestAsync(routingId, parts, DefaultRequestTimeout, ct);

    public Task<Received> RequestAsync(RoutingId routingId, IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct = default)
        => RequestCoreAsync(routingId, parts, timeout, ct, trySend: false);

    public Task<Received> TryRequestAsync(RoutingId routingId, Message message,
        CancellationToken ct = default)
        => TryRequestAsync(routingId, new[] { message }, DefaultRequestTimeout, ct);

    public Task<Received> TryRequestAsync(RoutingId routingId, Message message,
        TimeSpan timeout, CancellationToken ct = default)
        => TryRequestAsync(routingId, new[] { message }, timeout, ct);

    public Task<Received> TryRequestAsync(RoutingId routingId, IReadOnlyList<Message> parts,
        CancellationToken ct = default)
        => TryRequestAsync(routingId, parts, DefaultRequestTimeout, ct);

    public Task<Received> TryRequestAsync(RoutingId routingId, IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct = default)
        => RequestCoreAsync(routingId, parts, timeout, ct, trySend: true);

    public void Request(RoutingId routingId, Message message,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => Request(routingId, new[] { message }, onReply, onError, DefaultRequestTimeout);

    public void Request(RoutingId routingId, Message message,
        RequestReplyCallback onReply, RequestErrorCallback onError, TimeSpan timeout)
        => Request(routingId, new[] { message }, onReply, onError, timeout);

    public void Request(RoutingId routingId, IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => Request(routingId, parts, onReply, onError, DefaultRequestTimeout);

    public void Request(RoutingId routingId, IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError, TimeSpan timeout)
        => RequestReplySupport.AttachCallback(() => RequestAsync(routingId, parts, timeout), onReply, onError);

    public void TryRequest(RoutingId routingId, Message message,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => TryRequest(routingId, new[] { message }, onReply, onError, DefaultRequestTimeout);

    public void TryRequest(RoutingId routingId, Message message,
        RequestReplyCallback onReply, RequestErrorCallback onError, TimeSpan timeout)
        => TryRequest(routingId, new[] { message }, onReply, onError, timeout);

    public void TryRequest(RoutingId routingId, IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => TryRequest(routingId, parts, onReply, onError, DefaultRequestTimeout);

    public void TryRequest(RoutingId routingId, IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError, TimeSpan timeout)
        => RequestReplySupport.AttachCallback(() => TryRequestAsync(routingId, parts, timeout), onReply, onError);

    public void Reply(RoutingId routingId, ulong correlationId, Message message)
        => Reply(routingId, correlationId, new[] { message });

    public void Reply(RoutingId routingId, ulong correlationId,
        IReadOnlyList<Message> parts)
    {
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        cloned[0].SetReply(correlationId);
        _socket.Send(routingId, cloned);
    }

    public SendResult TryReply(RoutingId routingId, ulong correlationId,
        Message message)
        => TryReply(routingId, correlationId, new[] { message });

    public SendResult TryReply(RoutingId routingId, ulong correlationId,
        IReadOnlyList<Message> parts)
    {
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        cloned[0].SetReply(correlationId);
        return _socket.TrySend(routingId, cloned);
    }

    public Received Recv()
    {
        if (_dataHandler != null)
            throw new InvalidOperationException("socket is in callback mode");
        try
        {
            return _dataQueue.Take(_stop.Token);
        }
        catch (OperationCanceledException)
        {
            throw new ZlinkException((int)ErrorCode.Eterm, "socket is closed");
        }
        catch (InvalidOperationException)
        {
            throw new ZlinkException((int)ErrorCode.Eterm, "socket is closed");
        }
    }

    public bool TryRecv(out Received? received)
    {
        if (_dataHandler != null)
            throw new InvalidOperationException("socket is in callback mode");
        return _dataQueue.TryTake(out received, 0);
    }

    public void OnReceive(SocketRecvHandler handler)
    {
        _dataHandler = handler ?? throw new ArgumentNullException(nameof(handler));
        _dataHandlerContext = SynchronizationContext.Current;
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        _stop.Cancel();
        _socket.Dispose();
        _dispatchThread.Join(TimeSpan.FromSeconds(1));
        _dataQueue.CompleteAdding();
        FailAll(new ZlinkException((int)ErrorCode.Eterm, "socket is closed"));
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    private Task<Received> RequestCoreAsync(RoutingId routingId,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        bool trySend)
    {
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        ulong correlationId = unchecked((ulong)Interlocked.Increment(ref _nextCorrelationId) - 1);
        cloned[0].SetRequest(correlationId);
        var pending = new PendingRequest();
        if (!_pending.TryAdd(correlationId, pending))
        {
            RequestReplySupport.DisposeParts(cloned);
            throw new InvalidOperationException("duplicate correlation id");
        }

        CancellationTokenRegistration ctr = default;
        Timer? timeoutTimer = null;
        if (ct.CanBeCanceled)
        {
            ctr = ct.Register(() =>
            {
                if (_pending.TryRemove(correlationId, out PendingRequest? removed))
                    removed.TrySetCanceled(ct);
            });
        }
        if (timeout <= TimeSpan.Zero)
            timeout = DefaultRequestTimeout;
        timeoutTimer = new Timer(_ =>
        {
            if (_pending.TryRemove(correlationId, out PendingRequest? removed))
            {
                removed.TrySetException(new ZlinkException((int)ErrorCode.ETimedOut,
                    "request timed out"));
            }
        }, null, timeout, Timeout.InfiniteTimeSpan);

        try
        {
            if (trySend)
            {
                SendResult result = _socket.TrySend(routingId, cloned);
                if (result != SendResult.Sent)
                    throw new ZlinkException((int)ErrorCode.EAgain,
                        "request send is backpressured");
            }
            else
            {
                _socket.Send(routingId, cloned);
            }
        }
        catch (Exception ex)
        {
            _pending.TryRemove(correlationId, out _);
            timeoutTimer.Dispose();
            ctr.Dispose();
            RequestReplySupport.DisposeParts(cloned);
            return Task.FromException<Received>(ex);
        }

        pending.Completion.Task.ContinueWith(_ =>
        {
            timeoutTimer.Dispose();
            ctr.Dispose();
        }, TaskScheduler.Default);
        return pending.Completion.Task;
    }

    private void DispatchLoop()
    {
        while (!_stop.IsCancellationRequested)
        {
            try
            {
                if (!_socket.TryRecv(out Received? received))
                {
                    Thread.Sleep(PollDelay);
                    continue;
                }
                if (received == null)
                    continue;
                RouteReceived(RequestReplySupport.CloneReceived(received));
            }
            catch (ZlinkException ex) when (ex.Errno == (int)ErrorCode.Eterm)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            catch (Exception ex)
            {
                FailAll(ex);
                break;
            }
        }
    }

    private void RouteReceived(Received received)
    {
        if (received.Parts.Count == 0)
        {
            DeliverData(received);
            return;
        }
        (byte msgType, ulong correlationId) = received.Parts[0].GetRequestInfo();
        if (msgType == 2 && _pending.TryRemove(correlationId, out PendingRequest? pending))
        {
            pending.TrySetResult(received);
            return;
        }
        DeliverData(received);
    }

    private void DeliverData(Received received)
    {
        SocketRecvHandler? handler = _dataHandler;
        if (handler == null)
        {
            if (!_dataQueue.IsAddingCompleted)
                _dataQueue.Add(received);
            return;
        }
        SynchronizationContext? context = _dataHandlerContext;
        CallbackDelivery.Post(context, () => handler(received.RoutingId,
            RequestReplySupport.ToArray(received.Parts)));
    }

    private void FailAll(Exception ex)
    {
        foreach ((ulong key, PendingRequest pending) in _pending)
        {
            if (_pending.TryRemove(key, out PendingRequest? removed))
                removed.TrySetException(ex);
        }
    }

}

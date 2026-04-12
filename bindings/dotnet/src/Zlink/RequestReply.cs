// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

internal delegate void RequestReplyCallback(Received reply);
internal delegate void RequestErrorCallback(ZlinkException error);

internal sealed class RequestCallState
{
    private CancellationTokenRegistration _cancellationRegistration;
    private System.Threading.Timer? _timeoutTimer;
    private int _completed;

    internal RequestCallState(TaskCompletionSource<Received> completion)
    {
        Completion = completion;
    }

    internal TaskCompletionSource<Received> Completion { get; }

    internal bool TrySetResult(Received received)
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0)
            return false;
        DisposeRegistrations();
        return Completion.TrySetResult(received);
    }

    internal bool TrySetException(Exception error)
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0)
            return false;
        DisposeRegistrations();
        return Completion.TrySetException(error);
    }

    internal bool TrySetCanceled(CancellationToken token)
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0)
            return false;
        DisposeRegistrations();
        return Completion.TrySetCanceled(token);
    }

    internal void SetCancellationRegistration(
        CancellationTokenRegistration cancellationRegistration)
    {
        _cancellationRegistration = cancellationRegistration;
    }

    internal void SetTimeoutTimer(System.Threading.Timer? timeoutTimer)
    {
        _timeoutTimer = timeoutTimer;
    }

    private void DisposeRegistrations()
    {
        _timeoutTimer?.Dispose();
        _cancellationRegistration.Dispose();
    }
}

public sealed class RequestDealer : IDisposable, IAsyncDisposable
{
    private static readonly TimeSpan DefaultTimeoutValue = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan PollDelay = TimeSpan.FromMilliseconds(1);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate ReplyHandler =
        OnReply;

    private readonly DealerSocket _socket;
    private readonly BlockingCollection<Received> _dataQueue = new();
    private readonly CancellationTokenSource _stop = new();
    private readonly Thread _dispatchThread;
    private Action<Received>? _dataHandler;
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
        => RequestCoreAsync(parts, timeout, ct, 0);

    public void Request(Message message, Action<RequestResult, Received?> callback,
        SendFlags flags = SendFlags.None, TimeSpan timeout = default)
        => Request(new[] { message }, callback, flags, timeout);

    public void Request(IReadOnlyList<Message> parts,
        Action<RequestResult, Received?> callback,
        SendFlags flags = SendFlags.None, TimeSpan timeout = default)
        => RequestReplySupport.AttachResultCallback(
            () => RequestCoreAsync(parts, timeout, CancellationToken.None,
                (int)flags),
            callback);

    internal Task<Received> TryRequestAsync(Message message,
        CancellationToken ct = default)
        => RequestAsync(message, ct);

    internal Task<Received> TryRequestAsync(Message message, TimeSpan timeout,
        CancellationToken ct = default)
        => RequestAsync(message, timeout, ct);

    internal Task<Received> TryRequestAsync(IReadOnlyList<Message> parts,
        CancellationToken ct = default)
        => RequestAsync(parts, ct);

    internal Task<Received> TryRequestAsync(IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct = default)
        => RequestAsync(parts, timeout, ct);

    internal void Request(Message message, RequestReplyCallback onReply,
        RequestErrorCallback onError)
        => Request(new[] { message }, onReply, onError, DefaultRequestTimeout);

    internal void Request(Message message, RequestReplyCallback onReply,
        RequestErrorCallback onError, TimeSpan timeout)
        => Request(new[] { message }, onReply, onError, timeout);

    internal void Request(IReadOnlyList<Message> parts, RequestReplyCallback onReply,
        RequestErrorCallback onError)
        => Request(parts, onReply, onError, DefaultRequestTimeout);

    internal void Request(IReadOnlyList<Message> parts, RequestReplyCallback onReply,
        RequestErrorCallback onError, TimeSpan timeout)
        => RequestReplySupport.AttachCallback(() => RequestAsync(parts, timeout),
            onReply, onError);

    internal void TryRequest(Message message, RequestReplyCallback onReply,
        RequestErrorCallback onError)
        => Request(message, onReply, onError);

    internal void TryRequest(Message message, RequestReplyCallback onReply,
        RequestErrorCallback onError, TimeSpan timeout)
        => Request(message, onReply, onError, timeout);

    internal void TryRequest(IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => Request(parts, onReply, onError);

    internal void TryRequest(IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError,
        TimeSpan timeout)
        => Request(parts, onReply, onError, timeout);

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
            throw new ZlinkCloseException(CloseResult.InvalidHandle,
                (int)ErrorCode.Eterm);
        }
        catch (InvalidOperationException)
        {
            throw new ZlinkCloseException(CloseResult.InvalidHandle,
                (int)ErrorCode.Eterm);
        }
    }

    internal bool TryRecv(out Received? received)
    {
        if (_dataHandler != null)
            throw new InvalidOperationException("socket is in callback mode");
        return _dataQueue.TryTake(out received, 0);
    }

    public void OnReceive(Action<Received> handler)
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
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    private unsafe Task<Received> RequestCoreAsync(IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, int flags)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));

        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        uint timeoutMs = NormalizeTimeout(timeout, DefaultRequestTimeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        RequestCallState? state = null;

        try
        {
            state = new RequestCallState(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            IntPtr userData = GCHandle.ToIntPtr(handle);

            if (ct.CanBeCanceled)
            {
                state.SetCancellationRegistration(ct.Register(static userdata =>
                {
                    RequestCallState callbackState = (RequestCallState)((GCHandle)userdata!).Target!;
                    callbackState.TrySetCanceled(CancellationToken.None);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState callbackState = (RequestCallState)((GCHandle)userdata!).Target!;
                callbackState.TrySetException(new ZlinkRequestException(
                    RequestResult.TimedOut, (int)ErrorCode.ETimedOut));
            }, handle, (int)timeoutMs, Timeout.Infinite));

            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_dealer_request(_socket.Handle,
                    (IntPtr)nativePtr, (nuint)nativeParts.Length,
                    ReplyHandler, userData, flags, timeoutMs);
                if (rc != 0)
                    throw ZlinkException.FromLastError();
            }
            return completion.Task;
        }
        catch
        {
            RequestReplySupport.RestoreManagedParts(cloned, nativeParts,
                nativeParts.Length);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
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
                DeliverData(RequestReplySupport.CloneReceived(received));
            }
            catch (ZlinkException ex) when (ZlinkException.MapErrorCode(
                ex.InternalErrno) is ErrorCode.Eterm or ErrorCode.ENotSup)
            {
                break;
            }
            catch (ZlinkException ex) when (ZlinkException.MapErrorCode(
                ex.InternalErrno) == ErrorCode.EBusy)
            {
                Thread.Sleep(PollDelay);
            }
            catch (ObjectDisposedException)
            {
                break;
            }
        }
    }

    private void DeliverData(Received received)
    {
        Action<Received>? handler = _dataHandler;
        if (handler == null)
        {
            if (!_dataQueue.IsAddingCompleted)
                _dataQueue.Add(received);
            return;
        }
        SynchronizationContext? context = _dataHandlerContext;
        CallbackDelivery.Post(context, () => handler(received));
    }

    private static uint NormalizeTimeout(TimeSpan timeout, TimeSpan fallback)
    {
        TimeSpan effective = timeout <= TimeSpan.Zero ? fallback : timeout;
        double millis = effective.TotalMilliseconds;
        if (millis <= 1)
            return 1;
        if (millis >= uint.MaxValue)
            return uint.MaxValue;
        return (uint)millis;
    }

    private static void OnReply(int errno, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        RequestCallState state = (RequestCallState)handle.Target!;
        try
        {
            if (errno != 0)
            {
                state.TrySetException(new ZlinkRequestException(
                    RequestResult.ProtocolError, errno));
                return;
            }
            Message[] replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            Received received = new(string.Empty, replyParts);
            if (!state.TrySetResult(received))
                RequestReplySupport.DisposeParts(replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
        }
    }
}

public sealed unsafe class RequestRouter : IDisposable, IAsyncDisposable
{
    private static readonly TimeSpan DefaultTimeoutValue = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan PollDelay = TimeSpan.FromMilliseconds(1);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate ReplyHandler =
        OnReply;
    private static readonly NativeMethods.ZlinkRouterRequestHandlerDelegate
        RouterRequestHandler = OnRouterRequest;

    private readonly RouterSocket _socket;
    private readonly BlockingCollection<Received> _dataQueue = new();
    private readonly CancellationTokenSource _stop = new();
    private readonly Thread _dispatchThread;
    private readonly GCHandle _selfHandle;
    private Action<Received>? _dataHandler;
    private SynchronizationContext? _dataHandlerContext;
    private int _closed;

    public RequestRouter(RouterSocket socket)
    {
        _socket = socket ?? throw new ArgumentNullException(nameof(socket));
        _selfHandle = GCHandle.Alloc(this, GCHandleType.Normal);
        int rc = NativeMethods.zlink_router_handler(_socket.Handle,
            RouterRequestHandler, GCHandle.ToIntPtr(_selfHandle));
        if (rc != 0)
        {
            _selfHandle.Free();
            throw ZlinkException.FromLastError();
        }
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

    public Task<Received> RequestAsync(RoutingId routingId,
        IReadOnlyList<Message> parts, CancellationToken ct = default)
        => RequestAsync(routingId, parts, DefaultRequestTimeout, ct);

    public Task<Received> RequestAsync(RoutingId routingId,
        IReadOnlyList<Message> parts, TimeSpan timeout,
        CancellationToken ct = default)
        => RequestCoreAsync(routingId, parts, timeout, ct, 0);

    public void Request(RoutingId routingId, Message message,
        Action<RequestResult, Received?> callback,
        SendFlags flags = SendFlags.None, TimeSpan timeout = default)
        => Request(routingId, new[] { message }, callback, flags, timeout);

    public void Request(RoutingId routingId, IReadOnlyList<Message> parts,
        Action<RequestResult, Received?> callback,
        SendFlags flags = SendFlags.None, TimeSpan timeout = default)
        => RequestReplySupport.AttachResultCallback(
            () => RequestCoreAsync(routingId, parts, timeout,
                CancellationToken.None, (int)flags),
            callback);

    internal Task<Received> TryRequestAsync(RoutingId routingId, Message message,
        CancellationToken ct = default)
        => RequestAsync(routingId, message, ct);

    internal Task<Received> TryRequestAsync(RoutingId routingId, Message message,
        TimeSpan timeout, CancellationToken ct = default)
        => RequestAsync(routingId, message, timeout, ct);

    internal Task<Received> TryRequestAsync(RoutingId routingId,
        IReadOnlyList<Message> parts, CancellationToken ct = default)
        => RequestAsync(routingId, parts, ct);

    internal Task<Received> TryRequestAsync(RoutingId routingId,
        IReadOnlyList<Message> parts, TimeSpan timeout,
        CancellationToken ct = default)
        => RequestAsync(routingId, parts, timeout, ct);

    internal void Request(RoutingId routingId, Message message,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => Request(routingId, new[] { message }, onReply, onError,
            DefaultRequestTimeout);

    internal void Request(RoutingId routingId, Message message,
        RequestReplyCallback onReply, RequestErrorCallback onError,
        TimeSpan timeout)
        => Request(routingId, new[] { message }, onReply, onError, timeout);

    internal void Request(RoutingId routingId, IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => Request(routingId, parts, onReply, onError, DefaultRequestTimeout);

    internal void Request(RoutingId routingId, IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError,
        TimeSpan timeout)
        => RequestReplySupport.AttachCallback(
            () => RequestAsync(routingId, parts, timeout), onReply, onError);

    internal void TryRequest(RoutingId routingId, Message message,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => Request(routingId, message, onReply, onError);

    internal void TryRequest(RoutingId routingId, Message message,
        RequestReplyCallback onReply, RequestErrorCallback onError,
        TimeSpan timeout)
        => Request(routingId, message, onReply, onError, timeout);

    internal void TryRequest(RoutingId routingId, IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError)
        => Request(routingId, parts, onReply, onError);

    internal void TryRequest(RoutingId routingId, IReadOnlyList<Message> parts,
        RequestReplyCallback onReply, RequestErrorCallback onError,
        TimeSpan timeout)
        => Request(routingId, parts, onReply, onError, timeout);

    public void Reply(RoutingId routingId, ulong requestSequence, Message message,
        SendFlags flags = SendFlags.None)
        => Reply(routingId, requestSequence, new[] { message }, flags);

    public unsafe void Reply(RoutingId routingId, ulong requestSequence,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        SendResult result = TryReply(routingId, requestSequence, parts);
        if (result != SendResult.Sent)
        {
            throw new ZlinkSubmitException(SubmitResult.Backpressured,
                (int)ErrorCode.EAgain);
        }
    }

    internal SendResult TryReply(RoutingId routingId, ulong requestSequence,
        Message message)
        => TryReply(routingId, requestSequence, new[] { message });

    internal unsafe SendResult TryReply(RoutingId routingId, ulong requestSequence,
        IReadOnlyList<Message> parts)
    {
        byte[] ridBytes = RoutingIdCodec.FromPublicString(routingId.Value,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(ridBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        try
        {
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_router_reply(_socket.Handle,
                    ref nativeRoutingId, requestSequence, (IntPtr)nativePtr,
                    (nuint)nativeParts.Length);
                if (rc == 0)
                    return SendResult.Sent;
                return RequestReplySupport.MapTrySendResult(
                    ZlinkException.FromLastError());
            }
        }
        catch
        {
            RequestReplySupport.RestoreManagedParts(cloned, nativeParts,
                nativeParts.Length);
            throw;
        }
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
            throw new ZlinkCloseException(CloseResult.InvalidHandle,
                (int)ErrorCode.Eterm);
        }
        catch (InvalidOperationException)
        {
            throw new ZlinkCloseException(CloseResult.InvalidHandle,
                (int)ErrorCode.Eterm);
        }
    }

    internal bool TryRecv(out Received? received)
    {
        if (_dataHandler != null)
            throw new InvalidOperationException("socket is in callback mode");
        return _dataQueue.TryTake(out received, 0);
    }

    public void OnReceive(Action<Received> handler)
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
        if (_selfHandle.IsAllocated)
            _selfHandle.Free();
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    private unsafe Task<Received> RequestCoreAsync(RoutingId routingId,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));

        byte[] ridBytes = RoutingIdCodec.FromPublicString(routingId.Value,
            nameof(routingId));
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(ridBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        uint timeoutMs = NormalizeTimeout(timeout, DefaultRequestTimeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        RequestCallState? state = null;

        try
        {
            state = new RequestCallState(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
            {
                state.SetCancellationRegistration(ct.Register(static userdata =>
                {
                    RequestCallState callbackState = (RequestCallState)((GCHandle)userdata!).Target!;
                    callbackState.TrySetCanceled(CancellationToken.None);
                }, handle));
            }
            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState callbackState = (RequestCallState)((GCHandle)userdata!).Target!;
                callbackState.TrySetException(new ZlinkRequestException(
                    RequestResult.TimedOut, (int)ErrorCode.ETimedOut));
            }, handle, (int)timeoutMs, Timeout.Infinite));
            IntPtr userData = GCHandle.ToIntPtr(handle);

            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_router_request(_socket.Handle,
                    ref nativeRoutingId, (IntPtr)nativePtr,
                    (nuint)nativeParts.Length, ReplyHandler, userData, flags,
                    timeoutMs);
                if (rc != 0)
                    throw ZlinkException.FromLastError();
            }
            return completion.Task;
        }
        catch
        {
            RequestReplySupport.RestoreManagedParts(cloned, nativeParts,
                nativeParts.Length);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
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
                DeliverData(RequestReplySupport.CloneReceived(received));
            }
            catch (ZlinkException ex) when (ZlinkException.MapErrorCode(
                ex.InternalErrno) is ErrorCode.Eterm or ErrorCode.ENotSup)
            {
                break;
            }
            catch (ZlinkException ex) when (ZlinkException.MapErrorCode(
                ex.InternalErrno) == ErrorCode.EBusy)
            {
                Thread.Sleep(PollDelay);
            }
            catch (ObjectDisposedException)
            {
                break;
            }
        }
    }

    private void DeliverData(Received received)
    {
        Action<Received>? handler = _dataHandler;
        if (handler == null)
        {
            if (!_dataQueue.IsAddingCompleted)
                _dataQueue.Add(received);
            return;
        }
        SynchronizationContext? context = _dataHandlerContext;
        CallbackDelivery.Post(context, () => handler(received));
    }

    private unsafe void HandleRouterRequest(ZlinkRoutingId* peerRoutingId,
        ulong requestSequence, IntPtr parts, nuint partCount)
    {
        Message[] managedParts = Message.FromNativeVector(parts, partCount);
        parts = IntPtr.Zero;
        partCount = 0;
        string routingId = peerRoutingId == null ? string.Empty :
            RoutingIdCodec.ToPublicString(
                NativeHelpers.ReadRoutingId(ref *peerRoutingId));
        DeliverData(new Received(routingId, managedParts, requestSequence, true));
    }

    private static uint NormalizeTimeout(TimeSpan timeout, TimeSpan fallback)
    {
        TimeSpan effective = timeout <= TimeSpan.Zero ? fallback : timeout;
        double millis = effective.TotalMilliseconds;
        if (millis <= 1)
            return 1;
        if (millis >= uint.MaxValue)
            return uint.MaxValue;
        return (uint)millis;
    }

    private static void OnReply(int errno, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        RequestCallState state = (RequestCallState)handle.Target!;
        try
        {
            if (errno != 0)
            {
                state.TrySetException(new ZlinkRequestException(
                    RequestResult.ProtocolError, errno));
                return;
            }
            Message[] replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            Received received = new(string.Empty, replyParts);
            if (!state.TrySetResult(received))
                RequestReplySupport.DisposeParts(replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
        }
    }

    private static unsafe void OnRouterRequest(ZlinkRoutingId* peerRoutingId,
        ulong requestSequence, IntPtr parts, nuint partCount, IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        RequestRouter router = (RequestRouter)handle.Target!;
        try
        {
            router.HandleRouterRequest(peerRoutingId, requestSequence, parts,
                partCount);
            parts = IntPtr.Zero;
            partCount = 0;
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
        }
    }
}

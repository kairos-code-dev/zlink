// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class SpotRouteBridge : ISpotRouteBridge
{
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate DirectReplyHandler =
        OnDirectReply;
    private static readonly IntPtr DirectReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(DirectReplyHandler);

    private readonly Dictionary<string, IntPtr> _endpointHandles = new();
    private readonly object _gate = new();
    private IntPtr _handle;

    internal SpotRouteBridge(SpotNode node, SpotRouteBridgeOptions? options)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        node.EnsureNotDisposed();
        var nativeOptions = ToNativeOptions(options);
        _handle = NativeMethods.zlink_spot_route_bridge_new(
            node.Context.Handle, node.Handle, ref nativeOptions);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void AttachRouterChannel(string channelName, IRouterSocket router,
        SpotRouteBridgeEndpointOptions? options = null)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        var concrete = SocketInterop.RequireRouterSocket(router,
            nameof(router));
        var nativeOptions =
            ToNativeEndpointOptions(options);
        var rc = NativeMethods.zlink_spot_route_bridge_attach_router_channel(
            _handle, channelName, concrete.Handle, ref nativeOptions);
        ZlinkException.ThrowConfigIfError(rc);
        lock (_gate)
        {
            _endpointHandles[channelName] = concrete.Handle;
        }
    }

    public bool Send(string channelName, RoutingId targetNodeRid,
        RoutingId targetSpotRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        try
        {
            SubmitParts(parts, (nativeParts, partCount) =>
            {
                var nativeNodeRid = targetNodeRid.ToNative();
                var nativeSpotRid = targetSpotRid.ToNative();
                return NativeMethods.zlink_spot_route_bridge_send(
                    _handle, channelName, ref nativeNodeRid,
                    ref nativeSpotRid, nativeParts, partCount, (int)flags);
            });
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            return false;
        }
    }

    public bool Request(string channelName, RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None,
        TimeSpan timeout = default)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var nativeNodeRid = targetNodeRid.ToNative();
        var nativeSpotRid = targetSpotRid.ToNative();
        var timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
        GCHandle handle = default;
        SpotRouteBridgeRequestCallbackState? state = null;
        try
        {
            state = new SpotRouteBridgeRequestCallbackState(
                callback,
                RequestProgressPump.AttachSocketCallback(EndpointHandle(channelName)));
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            var userData = GCHandle.ToIntPtr(handle);

            SubmitParts(parts, (nativeParts, partCount) =>
                NativeMethods.zlink_spot_route_bridge_request(
                    _handle,
                    channelName,
                    ref nativeNodeRid,
                    ref nativeSpotRid,
                    nativeParts,
                    partCount,
                    DirectReplyHandlerPtr,
                    userData,
                    (int)flags,
                    timeoutMs));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            state?.DisposeProgress();
            if (handle.IsAllocated)
                handle.Free();
            return false;
        }
        catch
        {
            state?.DisposeProgress();
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    public bool HandleRouterReceived(string channelName, RoutingId sourceNodeRid,
        ulong requestSeq, IReadOnlyList<Message> parts)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        var handled = false;
        SubmitParts(parts, (nativeParts, partCount) =>
        {
            var nativeSourceRid = sourceNodeRid.ToNative();
            var rc = NativeMethods
                .zlink_spot_route_bridge_handle_router_received(
                    _handle, channelName, ref nativeSourceRid, requestSeq,
                    nativeParts, partCount, out handled);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            return rc;
        }, false);
        return handled;
    }

    public void Drain()
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_route_bridge_drain(_handle);
        if (rc < 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void Close()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    public void Dispose()
    {
        Close();
    }

    public ValueTask DisposeAsync()
    {
        Close();
        return ValueTask.CompletedTask;
    }

    ~SpotRouteBridge()
    {
        Dispose(false);
    }

    private async Task<Received> RequestAsyncInternal(string channelName,
        RoutingId targetNodeRid, RoutingId targetSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags, TimeSpan timeout)
    {
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        RequestCallState state = new(completion);
        var handle = GCHandle.Alloc(state);
        var submitted = false;
        try
        {
            var timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
            SubmitParts(parts, (nativeParts, partCount) =>
            {
                var nativeNodeRid = targetNodeRid.ToNative();
                var nativeSpotRid = targetSpotRid.ToNative();
                return NativeMethods.zlink_spot_route_bridge_request(
                    _handle, channelName, ref nativeNodeRid,
                    ref nativeSpotRid, nativeParts, partCount, Spot.RoutedReplyHandlerPointer,
                    GCHandle.ToIntPtr(handle), (int)flags, timeoutMs);
            });
            submitted = true;
            var progressHandle = EndpointHandle(channelName);
            return await RequestProgressPump
                .AttachSocket(progressHandle, completion.Task)
                .ConfigureAwait(false);
        }
        finally
        {
            if (!submitted && handle.IsAllocated)
                handle.Free();
        }
    }

    private static void OnDirectReply(int result, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        var handle = GCHandle.FromIntPtr(userData);
        var state = (SpotRouteBridgeRequestCallbackState)handle.Target!;
        try
        {
            if (!state.TryStartCompletion())
                return;

            if (result != 0)
            {
                state.Invoke((RequestResult)result, Array.Empty<Message>());
                return;
            }

            var replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            state.Invoke(RequestResult.Ok, replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
        }
    }

    private IntPtr EndpointHandle(string channelName)
    {
        lock (_gate)
        {
            return _endpointHandles.TryGetValue(channelName, out var handle)
                ? handle
                : IntPtr.Zero;
        }
    }

    private static void SubmitParts(IReadOnlyList<Message> parts,
        NativeBridgeSubmitter submit, bool throwSubmit = true)
    {
        NativeMessageParts.SubmitClonedVector(parts, nameof(parts),
            (nativeParts, partCount) => submit(nativeParts, partCount),
            throwSubmit ? ThrowBridgeSubmitIfError : null);
    }

    private static void ThrowBridgeSubmitIfError(int rc)
    {
        if (rc != 0)
            throw ZlinkException.CreateSubmitException(
                NativeMethods.zlink_errno());
    }

    private static ZlinkSpotRouteBridgeOptions ToNativeOptions(
        SpotRouteBridgeOptions? options)
    {
        return new ZlinkSpotRouteBridgeOptions
        {
            StructSize = (uint)Marshal.SizeOf<ZlinkSpotRouteBridgeOptions>(),
            DefaultRequestTimeoutMs = options?.DefaultRequestTimeout == null
                ? 0
                : checked((int)RequestReplySupport.NormalizeTimeout(
                    options.DefaultRequestTimeout)),
            ErrorReplyPolicy = options?.ErrorReplyPolicy ?? 0,
            ReceiveMode = options?.ReceiveMode ?? 0
        };
    }

    private static ZlinkSpotRouteBridgeEndpointOptions ToNativeEndpointOptions(
        SpotRouteBridgeEndpointOptions? options)
    {
        return new ZlinkSpotRouteBridgeEndpointOptions
        {
            StructSize =
                (uint)Marshal.SizeOf<ZlinkSpotRouteBridgeEndpointOptions>(),
            Capabilities = options == null
                ? NativeMethods.SpotRouteBridgeRouteOnly
                : (uint)options.Capabilities,
            InboundRelayPolicy = options?.InboundRelayPolicy ?? 0
        };
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SpotRouteBridge));
    }

    private void Dispose(bool disposing)
    {
        _ = disposing;
        var handle = _handle;
        if (handle == IntPtr.Zero)
            return;
        _handle = IntPtr.Zero;
        var rc = NativeMethods.zlink_spot_route_bridge_close(handle);
        if (rc != 0 && disposing)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    private delegate int NativeBridgeSubmitter(IntPtr parts, nuint partCount);

    private sealed class SpotRouteBridgeRequestCallbackState
    {
        private readonly Action<RequestResult, IReadOnlyList<Message>> _callback;
        private readonly SynchronizationContext? _callbackContext;
        private readonly RequestProgressPump.ProgressLease _progress;
        private int _completed;

        internal SpotRouteBridgeRequestCallbackState(
            Action<RequestResult, IReadOnlyList<Message>> callback,
            RequestProgressPump.ProgressLease progress)
        {
            _callback = callback;
            _callbackContext = SynchronizationContext.Current;
            _progress = progress;
        }

        internal bool TryStartCompletion()
        {
            if (Interlocked.Exchange(ref _completed, 1) != 0)
                return false;
            DisposeProgress();
            return true;
        }

        internal void DisposeProgress()
        {
            _progress.Dispose();
        }

        internal void Invoke(RequestResult result, IReadOnlyList<Message> payload)
        {
            CallbackDelivery.Post(_callbackContext,
                () => _callback(result, payload));
        }
    }
}

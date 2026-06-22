// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class SpotRouteBridge : ISpotRouteBridge
{
    private const int StackPartLimit = 8;
    private readonly object _gate = new();
    private readonly Dictionary<string, IntPtr> _endpointHandles = new();
    private IntPtr _handle;

    internal SpotRouteBridge(SpotNode node, SpotRouteBridgeOptions? options)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        node.EnsureNotDisposed();
        ZlinkSpotRouteBridgeOptions nativeOptions = ToNativeOptions(options);
        _handle = NativeMethods.zlink_spot_route_bridge_new(
            node.Context.Handle, node.Handle, ref nativeOptions);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    ~SpotRouteBridge()
    {
        Dispose(false);
    }

    public void AttachDealerChannel(string channelName, IDealerSocket dealer,
        SpotRouteBridgeEndpointOptions? options = null)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        DealerSocket concrete = SocketInterop.RequireDealerSocket(dealer,
            nameof(dealer));
        ZlinkSpotRouteBridgeEndpointOptions nativeOptions =
            ToNativeEndpointOptions(options);
        int rc = NativeMethods.zlink_spot_route_bridge_attach_dealer_channel(
            _handle, channelName, concrete.Handle, ref nativeOptions);
        ZlinkException.ThrowConfigIfError(rc);
        lock (_gate)
            _endpointHandles[channelName] = concrete.Handle;
    }

    public void AttachRouterChannel(string channelName, IRouterSocket router,
        SpotRouteBridgeEndpointOptions? options = null)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        RouterSocket concrete = SocketInterop.RequireRouterSocket(router,
            nameof(router));
        ZlinkSpotRouteBridgeEndpointOptions nativeOptions =
            ToNativeEndpointOptions(options);
        int rc = NativeMethods.zlink_spot_route_bridge_attach_router_channel(
            _handle, channelName, concrete.Handle, ref nativeOptions);
        ZlinkException.ThrowConfigIfError(rc);
        lock (_gate)
            _endpointHandles[channelName] = concrete.Handle;
    }

    public void SetTargetNode(string channelName, RoutingId targetNodeRid)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = targetNodeRid.ToNative();
        int rc = NativeMethods.zlink_spot_route_bridge_set_target_node(
            _handle, channelName, ref nativeRid);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public bool Send(string channelName, RoutingId targetSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        try
        {
            SubmitParts(parts, (nativeParts, partCount) =>
            {
                ZlinkRoutingId nativeSpotRid = targetSpotRid.ToNative();
                unsafe
                {
                    return NativeMethods.zlink_spot_route_bridge_send(
                        _handle, channelName, ref nativeSpotRid, nativeParts,
                        partCount, (int)flags);
                }
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

    public bool Request(string channelName, RoutingId targetSpotRid,
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
        Message[] ownedParts = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => Task.Run(async () =>
                {
                    try
                    {
                        return await RequestAsyncInternal(channelName,
                            targetSpotRid, ownedParts, flags, timeout)
                            .ConfigureAwait(false);
                    }
                    finally
                    {
                        RequestReplySupport.DisposeParts(ownedParts);
                    }
                }),
                (result, reply) =>
                {
                    IReadOnlyList<Message> payload = Array.Empty<Message>();
                    if (reply != null)
                    {
                        payload = RequestReplySupport.TakeOwnedParts(reply);
                        reply.Dispose();
                    }
                    callback(result, payload);
                });
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            RequestReplySupport.DisposeParts(ownedParts);
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(ownedParts);
            throw;
        }
    }

    public bool HandleRouterReceived(string channelName, RoutingId sourceNodeRid,
        IReadOnlyList<Message> parts)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        bool handled = false;
        SubmitParts(parts, (nativeParts, partCount) =>
        {
            ZlinkRoutingId nativeSourceRid = sourceNodeRid.ToNative();
            unsafe
            {
                int rc = NativeMethods.zlink_spot_route_bridge_handle_router_received(
                    _handle, channelName, ref nativeSourceRid, nativeParts,
                    partCount, out handled);
                if (rc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                return rc;
            }
        }, throwSubmit: false);
        return handled;
    }

    public bool HandleRouterReceived(string channelName, RoutingId sourceNodeRid,
        ulong requestSeq, IReadOnlyList<Message> parts)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        bool handled = false;
        SubmitParts(parts, (nativeParts, partCount) =>
        {
            ZlinkRoutingId nativeSourceRid = sourceNodeRid.ToNative();
            unsafe
            {
                int rc = NativeMethods
                    .zlink_spot_route_bridge_handle_router_received_with_metadata(
                        _handle, channelName, ref nativeSourceRid, requestSeq,
                        nativeParts, partCount, out handled);
                if (rc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                return rc;
            }
        }, throwSubmit: false);
        return handled;
    }

    public bool HandleDealerReceived(string channelName,
        IReadOnlyList<Message> parts)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        bool handled = false;
        SubmitParts(parts, (nativeParts, partCount) =>
        {
            unsafe
            {
                int rc = NativeMethods.zlink_spot_route_bridge_handle_dealer_received(
                    _handle, channelName, nativeParts, partCount, out handled);
                if (rc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                return rc;
            }
        }, throwSubmit: false);
        return handled;
    }

    public bool HandleDealerReceived(string channelName,
        ReceivedMessageType messageType, ulong requestSeq,
        IReadOnlyList<Message> parts)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        bool handled = false;
        SubmitParts(parts, (nativeParts, partCount) =>
        {
            unsafe
            {
                int rc = NativeMethods
                    .zlink_spot_route_bridge_handle_dealer_received_with_metadata(
                        _handle, channelName, (byte)messageType, requestSeq,
                        nativeParts, partCount, out handled);
                if (rc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                return rc;
            }
        }, throwSubmit: false);
        return handled;
    }

    public void Drain()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_route_bridge_drain(_handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public SpotRouteBridgeSummary Summary()
    {
        EnsureNotDisposed();
        ZlinkSpotRouteBridgeSummary nativeSummary = new()
        {
            StructSize = (uint)Marshal.SizeOf<ZlinkSpotRouteBridgeSummary>()
        };
        int rc = NativeMethods.zlink_spot_route_bridge_summary(
            _handle, ref nativeSummary);
        ZlinkException.ThrowConfigIfError(rc);
        return new SpotRouteBridgeSummary(nativeSummary.AttachedChannelCount,
            nativeSummary.PendingRequestCount, nativeSummary.RejectedInboundCount,
            nativeSummary.MalformedInboundCount,
            nativeSummary.RoutedSendFailureCount);
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

    private async Task<Received> RequestAsyncInternal(string channelName,
        RoutingId targetSpotRid, IReadOnlyList<Message> parts, SendFlags flags,
        TimeSpan timeout)
    {
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        RequestCallState state = new(completion);
        GCHandle handle = GCHandle.Alloc(state);
        bool submitted = false;
        try
        {
            uint timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
            SubmitParts(parts, (nativeParts, partCount) =>
            {
                ZlinkRoutingId nativeSpotRid = targetSpotRid.ToNative();
                unsafe
                {
                    return NativeMethods.zlink_spot_route_bridge_request(
                        _handle, channelName, ref nativeSpotRid, nativeParts,
                        partCount, Spot.RoutedReplyHandlerPointer,
                        GCHandle.ToIntPtr(handle), (int)flags, timeoutMs);
                }
            });
            submitted = true;
            IntPtr progressHandle = EndpointHandle(channelName);
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

    private IntPtr EndpointHandle(string channelName)
    {
        lock (_gate)
        {
            return _endpointHandles.TryGetValue(channelName, out IntPtr handle)
                ? handle
                : IntPtr.Zero;
        }
    }

    private delegate int NativeBridgeSubmitter(IntPtr parts, nuint partCount);

    private static unsafe void SubmitParts(IReadOnlyList<Message> parts,
        NativeBridgeSubmitter submit, bool throwSubmit = true)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        if (submit == null)
            throw new ArgumentNullException(nameof(submit));

        Message[] cloned = RequestReplySupport.CloneParts(parts);
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = cloned.Length <= StackPartLimit
            ? stackalloc ZlinkMsg[StackPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(cloned.Length));
        nativeParts = nativeParts.Slice(0, cloned.Length);
        int built = 0;
        try
        {
            NativeMessageParts.MoveToNative(cloned, nativeParts, nameof(parts),
                ref built);
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = submit((IntPtr)nativePtr, (nuint)built);
                if (rc != 0 && throwSubmit)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        finally
        {
            for (int i = 0; i < built; i++)
                NativeMethods.zlink_msg_close(ref nativeParts[i]);
            RequestReplySupport.DisposeParts(cloned);
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
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
        IntPtr handle = _handle;
        if (handle == IntPtr.Zero)
            return;
        _handle = IntPtr.Zero;
        int rc = NativeMethods.zlink_spot_route_bridge_close(handle);
        if (rc != 0 && disposing)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }
}

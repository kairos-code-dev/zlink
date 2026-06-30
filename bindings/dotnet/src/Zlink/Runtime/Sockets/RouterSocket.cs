// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class RouterSocket : ConnectableRoutedMessageSocketBase,
    IRouterSocket
{
    private static readonly TimeSpan DefaultRequestTimeout = TimeSpan.FromSeconds(5);

    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RequestReplyHandler =
        OnRequestReply;

    private static readonly NativeMethods.ZlinkReplyHandlerDelegate SpotReplyHandler =
        OnSpotReply;

    private static readonly IntPtr RequestReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RequestReplyHandler);

    private static readonly IntPtr SpotReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(SpotReplyHandler);

    public RouterSocket(Context context)
        : base(context, SocketType.Router)
    {
        Options = new RouterSocketOptions(this);
    }

    public new RouterSocketOptions Options { get; }

    public void AttachDiscovery(IDiscovery discovery)
    {
        Kernel.AttachDiscovery(SocketInterop.RequireDiscovery(discovery,
            nameof(discovery)));
    }

    public void SetRoutingId(RoutingId routingId)
    {
        Kernel.SetOption(SocketOptions.RoutingId, routingId.ToBytes());
    }

    public RoutingId GetRoutingId()
    {
        return RoutingId.From(Kernel.GetOption(SocketOptions.RoutingId));
    }

    /// <summary>
    ///     Start a request to a specific peer (operation builder).
    /// </summary>
    public RequestOperation Request(RoutingId peerRid)
    {
        return new RouterRequestOperation(this, RouterOperationKind.Request,
            peerRid, default, default);
    }

    /// <summary>
    ///     Start a reply (operation builder).
    /// </summary>
    public ReplyOperation Reply(RoutingId rid, ulong requestSeq)
    {
        return new RouterReplyOperation(this, RouterOperationKind.Reply, rid,
            default, default, requestSeq);
    }

    /// <summary>
    ///     Start a router -> spot routed send (operation builder).
    /// </summary>
    public SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid)
    {
        return new RouterSendOperation(this, destNodeRid, destSpotRid);
    }

    /// <summary>
    ///     Start a router -> spot routed request (operation builder).
    /// </summary>
    public RequestOperation RequestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid)
    {
        return new RouterRequestOperation(this,
            RouterOperationKind.RequestToSpot, default, destNodeRid,
            destSpotRid);
    }

    /// <summary>
    ///     Start a router -> spot routed reply (operation builder).
    /// </summary>
    public ReplyOperation ReplyToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid, ulong requestSeq)
    {
        return new RouterReplyOperation(this, RouterOperationKind.ReplyToSpot,
            default, destNodeRid, destSpotRid, requestSeq);
    }

    internal async Task<IReadOnlyList<Message>> RequestCore(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct)
    {
        var received = await RequestAsyncCore(peerRid, parts,
                timeout == TimeSpan.Zero ? DefaultRequestTimeout : timeout, ct)
            .ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestCallbackCore(RoutingId peerRid,
        IReadOnlyList<Message> parts, RequestCallback callback,
        SendFlags flags, TimeSpan timeout)
    {
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestAsyncCore(peerRid, parts,
                    timeout == TimeSpan.Zero ? TimeSpan.Zero : timeout,
                    CancellationToken.None, (int)flags),
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
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0)
        {
            if (RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
                return false;

            throw;
        }
    }

    internal void ReplyCore(RoutingId peerRid, ulong requestSeq,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var nativeRoutingId = peerRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            lock (SubmitGate)
            {
                RequestReplySupport.SubmitClonedParts(cloned,
                    (ref ZlinkMsg nativePart,
                            NativeMethods.ZlinkPartFlag partFlag) =>
                        NativeMethods.zlink_router_reply_part(Handle,
                            ref nativeRoutingId, requestSeq, ref nativePart,
                            partFlag));
            }
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    internal bool SendToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var nodeRid = destNodeRid.ToNative();
        var spotRid = destSpotRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            lock (SubmitGate)
            {
                RequestReplySupport.SubmitClonedParts(cloned,
                    (ref ZlinkMsg nativePart,
                            NativeMethods.ZlinkPartFlag partFlag) =>
                        NativeMethods.zlink_router_send_spot_part(Handle,
                            ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                            partFlag));
            }

            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            RequestReplySupport.DisposeParts(cloned);
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    internal async Task<IReadOnlyList<Message>> RequestToSpotCore(
        RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct)
    {
        var received = await RequestToSpotAsyncInternal(destNodeRid,
            destSpotRid, parts, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToSpotCallbackCore(RoutingId destNodeRid,
        RoutingId destSpotRid, IReadOnlyList<Message> parts,
        RequestCallback callback, SendFlags flags, TimeSpan timeout)
    {
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestToSpotAsyncInternal(destNodeRid, destSpotRid, parts,
                    timeout, CancellationToken.None, (int)flags),
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
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0)
        {
            if (RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
                return false;

            throw;
        }
    }

    internal void ReplyToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, ulong requestSeq, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        _ = flags;
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var nodeRid = destNodeRid.ToNative();
        var spotRid = destSpotRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            lock (SubmitGate)
            {
                RequestReplySupport.SubmitClonedParts(cloned,
                    (ref ZlinkMsg nativePart,
                            NativeMethods.ZlinkPartFlag partFlag) =>
                        NativeMethods.zlink_router_reply_spot_part(Handle,
                            ref nodeRid, ref spotRid, requestSeq, ref nativePart,
                            partFlag));
            }
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private Task<Received> RequestToSpotAsyncInternal(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var nodeRid = destNodeRid.ToNative();
        var spotRid = destSpotRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        var timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        RequestCallState? state = null;

        try
        {
            state = new RequestCallState(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
                state.SetCancellationRegistration(
                    ct.Register(static userdata => { RequestCallState.CancelFromUserData(userdata); }, handle));

            state.SetTimeoutTimer(new System.Threading.Timer(
                static userdata => { RequestCallState.TimeoutFromUserData(userdata); }, handle, (int)timeoutMs,
                Timeout.Infinite));

            lock (SubmitGate)
            {
                for (var i = 0; i < cloned.Length; i++)
                {
                    ZlinkMsg nativePart = default;
                    cloned[i].MoveTo(ref nativePart);
                    var submitted = false;
                    try
                    {
                        var rc = NativeMethods.zlink_router_request_spot_part(Handle,
                            ref nodeRid, ref spotRid, ref nativePart,
                            SpotReplyHandlerPtr,
                            GCHandle.ToIntPtr(handle),
                            flags,
                            i + 1 < cloned.Length
                                ? NativeMethods.ZlinkPartFlag.More
                                : NativeMethods.ZlinkPartFlag.Final,
                            timeoutMs);
                        submitted = true;
                        if (rc != 0)
                            throw ZlinkException.CreateSubmitException(
                                NativeMethods.zlink_errno());
                    }
                    finally
                    {
                        if (!submitted)
                            NativeMethods.zlink_msg_close(ref nativePart);
                    }
                }
            }

            return RequestProgressPump.AttachSocket(Handle, completion.Task);
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private Task<Received> RequestAsyncCore(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var nativeRoutingId = peerRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        var timeoutMs = RequestReplySupport.NormalizeRequestTimeout(timeout,
            DefaultRequestTimeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;

        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);

            if (ct.CanBeCanceled)
                state.SetCancellationRegistration(
                    ct.Register(static userdata => { RequestCallState.CancelFromUserData(userdata); }, handle));

            state.SetTimeoutTimer(new System.Threading.Timer(
                static userdata => { RequestCallState.RequestTimeoutFromUserData(userdata); }, handle, (int)timeoutMs,
                Timeout.Infinite));

            var userData = GCHandle.ToIntPtr(handle);
            lock (SubmitGate)
            {
                RequestReplySupport.SubmitClonedParts(cloned,
                    (ref ZlinkMsg nativePart,
                            NativeMethods.ZlinkPartFlag partFlag) =>
                        NativeMethods.zlink_router_request_part(Handle,
                            ref nativeRoutingId, ref nativePart, flags, partFlag,
                            timeoutMs, RequestReplyHandlerPtr, userData));
            }

            return RequestProgressPump.AttachSocket(Handle, completion.Task);
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private static void OnSpotReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        RequestReplySupport.CompleteReceivedReply(result, parts, partCount,
            userData);
    }

    private static void OnRequestReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        RequestReplySupport.CompleteReceivedReply(result, parts, partCount,
            userData);
    }
}
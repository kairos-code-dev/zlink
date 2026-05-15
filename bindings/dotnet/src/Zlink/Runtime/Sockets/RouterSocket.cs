// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class RouterSocket : ConnectableRoutedMessageSocketBase,
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
    public new RouterSocketOptions Options { get; }

    public RouterSocket(Context context)
        : base(context, SocketType.Router)
    {
        Options = new RouterSocketOptions(this);
    }

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
        return RoutingId.FromBytes(Kernel.GetOption(SocketOptions.RoutingId));
    }

    /// <summary>
    /// Start a request to a specific peer (operation builder).
    /// </summary>
    public RequestOperation Request(RoutingId peerRid)
    {
        return new RouterRequestOperation(this, RouterOperationKind.Request,
            peerRid, default, default);
    }

    /// <summary>
    /// Start a reply (operation builder).
    /// </summary>
    public ReplyOperation Reply(RoutingId rid, ulong requestSeq)
    {
        return new RouterReplyOperation(this, RouterOperationKind.Reply, rid,
            default, default, requestSeq);
    }

    /// <summary>
    /// Start a router -> spot routed send (operation builder).
    /// </summary>
    public SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid)
    {
        return new RouterSendOperation(this, destNodeRid, destSpotRid);
    }

    /// <summary>
    /// Start a router -> spot routed request (operation builder).
    /// </summary>
    public RequestOperation RequestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid)
    {
        return new RouterRequestOperation(this,
            RouterOperationKind.RequestToSpot, default, destNodeRid,
            destSpotRid);
    }

    /// <summary>
    /// Start a router -> spot routed reply (operation builder).
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
        Received received = await RequestAsyncCore(peerRid, parts,
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
            {
                return false;
            }

            throw;
        }
    }

    internal unsafe void ReplyCore(RoutingId peerRid, ulong requestSeq,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nativeRoutingId = peerRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_router_reply_part(Handle,
                        ref nativeRoutingId, requestSeq, ref nativePart,
                        partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    internal unsafe bool SendToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_router_send_spot_part(Handle,
                        ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                        partFlag));
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
        Received received = await RequestToSpotAsyncInternal(destNodeRid,
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
            {
                return false;
            }

            throw;
        }
    }

    internal unsafe void ReplyToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, ulong requestSeq, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_router_reply_spot_part(Handle,
                        ref nodeRid, ref spotRid, requestSeq, ref nativePart,
                        partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe Task<Received> RequestToSpotAsyncInternal(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        uint timeoutMs = NormalizeTimeout(timeout);
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
                    RequestCallState.CancelFromUserData(userdata);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState.TimeoutFromUserData(userdata);
            }, handle, (int)timeoutMs, Timeout.Infinite));

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_router_request_spot_part(Handle,
                        ref nodeRid, ref spotRid, ref nativePart,
                        i + 1 < cloned.Length ? IntPtr.Zero : SpotReplyHandlerPtr,
                        i + 1 < cloned.Length ? IntPtr.Zero : GCHandle.ToIntPtr(handle),
                        flags,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final,
                        i + 1 < cloned.Length ? 0u : timeoutMs);
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

    private unsafe Task<Received> RequestAsyncCore(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nativeRoutingId = peerRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        uint timeoutMs = NormalizeRequestTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;

        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);

            if (ct.CanBeCanceled)
            {
                state.SetCancellationRegistration(ct.Register(static userdata =>
                {
                    RequestCallState.CancelFromUserData(userdata);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState.RequestTimeoutFromUserData(userdata);
            }, handle, (int)timeoutMs, Timeout.Infinite));

            IntPtr userData = GCHandle.ToIntPtr(handle);
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_router_request_part(Handle,
                        ref nativeRoutingId, ref nativePart, flags, partFlag,
                        timeoutMs, RequestReplyHandlerPtr, userData));

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

    private static uint NormalizeTimeout(TimeSpan timeout)
    {
        return BoundaryValidation.EncodeTimeoutMilliseconds(timeout,
            nameof(timeout));
    }

    private static uint NormalizeRequestTimeout(TimeSpan timeout)
    {
        TimeSpan effective = timeout == TimeSpan.Zero
            ? DefaultRequestTimeout
            : timeout;
        return BoundaryValidation.EncodeTimeoutMilliseconds(effective,
            nameof(timeout));
    }

    private static void EnsureParts(IReadOnlyList<Message> parts, string paramName)
    {
        if (parts == null)
            throw new ArgumentNullException(paramName);
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", paramName);
    }

    private static void OnSpotReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        RequestCallState state = (RequestCallState)handle.Target!;
        try
        {
            if (result != 0)
            {
                state.TrySetException(new ZlinkRequestException(
                    (RequestResult)result));
                return;
            }

            Message[] replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            Received received = Received.Create((RoutingId?)null, replyParts);
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

    private static void OnRequestReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        RequestCallState state = (RequestCallState)handle.Target!;
        try
        {
            if (result != 0)
            {
                state.TrySetException(new ZlinkRequestException(
                    (RequestResult)result));
                return;
            }

            Message[] replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            Received received = Received.Create((RoutingId?)null, replyParts);
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

// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class RouterSocket : ConnectableRoutedMessageSocketBase
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
    public RouterSocketOptions RouterOptions { get; }

    public RouterSocket(Context context)
        : base(context, SocketType.Router)
    {
        RouterOptions = new RouterSocketOptions(this);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        Kernel.AttachDiscovery(discovery);
    }

    public Task<IReadOnlyList<Message>> RequestAsync(RoutingId peerRid,
        Message part, CancellationToken ct = default)
        => RequestAsync(peerRid, new[] { part }, ct);

    public Task<IReadOnlyList<Message>> RequestAsync(RoutingId peerRid,
        Message part, TimeSpan timeout, CancellationToken ct = default)
        => RequestAsync(peerRid, new[] { part }, timeout, ct);

    public async Task<IReadOnlyList<Message>> RequestAsync(RoutingId peerRid,
        IReadOnlyList<Message> parts, CancellationToken ct = default)
    {
        Received received = await RequestAsyncCore(peerRid, parts,
            DefaultRequestTimeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    public async Task<IReadOnlyList<Message>> RequestAsync(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout,
        CancellationToken ct = default)
    {
        Received received = await RequestAsyncCore(peerRid, parts, timeout, ct)
            .ConfigureAwait(false);
        return received.Parts;
    }

    public bool Request(RoutingId peerRid, Message part,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => Request(peerRid, part, callback, SendFlags.None, timeout);

    public bool Request(RoutingId peerRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => Request(peerRid, parts, callback, SendFlags.None, timeout);

    public bool Request(RoutingId peerRid, Message part,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
        => Request(peerRid, new[] { part }, callback, flags, timeout);

    public bool Request(RoutingId peerRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
    {
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestAsyncCore(peerRid, parts, timeout ?? TimeSpan.Zero,
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

    public void Reply(RoutingId peerRid, ulong requestSeq, Message message,
        SendFlags flags = SendFlags.None)
        => Reply(peerRid, requestSeq, new[] { message }, flags);

    public unsafe void Reply(RoutingId peerRid, ulong requestSeq,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        byte[] ridBytes = RoutingIdCodec.FromRoutingId(peerRid);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(ridBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_router_reply_part(Handle,
                        ref nativeRoutingId, requestSeq, ref nativePart,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final);
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
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    public bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, SendFlags flags = SendFlags.None)
        => SendToSpot(destNodeRid, destSpotRid, new[] { message }, flags);

    public unsafe bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        EnsureParts(parts, nameof(parts));
        byte[] nodeRidBytes = RoutingIdCodec.FromRoutingId(destNodeRid);
        byte[] spotRidBytes = RoutingIdCodec.FromRoutingId(destSpotRid);
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(nodeRidBytes);
        ZlinkRoutingId spotRid = NativeHelpers.WriteRoutingId(spotRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_router_send_spot_part(Handle,
                        ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final);
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
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    public Task<IReadOnlyList<Message>> RequestToSpotAsync(RoutingId destNodeRid,
        RoutingId destSpotRid, Message message, TimeSpan timeout = default,
        CancellationToken ct = default)
        => RequestToSpotAsync(destNodeRid, destSpotRid, new[] { message }, timeout,
            ct);

    public Task<IReadOnlyList<Message>> RequestToSpotAsync(RoutingId destNodeRid,
        RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout = default, CancellationToken ct = default)
        => RequestToSpotAsyncInternal(destNodeRid, destSpotRid, parts, timeout, ct)
            .ContinueWith(task => task.Result.Parts, TaskScheduler.Default);

    public bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan timeout = default)
        => RequestToSpot(destNodeRid, destSpotRid, message, callback,
            SendFlags.None, timeout);

    public bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan timeout = default)
        => RequestToSpot(destNodeRid, destSpotRid, parts, callback,
            SendFlags.None, timeout);

    public bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan timeout = default)
        => RequestToSpot(destNodeRid, destSpotRid, new[] { message }, callback,
            flags, timeout);

    public bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan timeout = default)
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

    public void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, Message message, SendFlags flags = SendFlags.None)
        => ReplyToSpot(destNodeRid, destSpotRid, requestSeq, new[] { message },
            flags);

    public unsafe void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        byte[] nodeRidBytes = RoutingIdCodec.FromRoutingId(destNodeRid);
        byte[] spotRidBytes = RoutingIdCodec.FromRoutingId(destSpotRid);
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(nodeRidBytes);
        ZlinkRoutingId spotRid = NativeHelpers.WriteRoutingId(spotRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_router_reply_spot_part(Handle,
                        ref nodeRid, ref spotRid, requestSeq, ref nativePart,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final);
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
        byte[] nodeRidBytes = RoutingIdCodec.FromRoutingId(destNodeRid);
        byte[] spotRidBytes = RoutingIdCodec.FromRoutingId(destSpotRid);
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(nodeRidBytes);
        ZlinkRoutingId spotRid = NativeHelpers.WriteRoutingId(spotRidBytes);
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
        byte[] ridBytes = RoutingIdCodec.FromRoutingId(peerRid);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(ridBytes);
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
            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_router_request_part(Handle,
                        ref nativeRoutingId, ref nativePart, flags,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final,
                        i + 1 < cloned.Length ? 0u : timeoutMs,
                        i + 1 < cloned.Length ? IntPtr.Zero : RequestReplyHandlerPtr,
                        i + 1 < cloned.Length ? IntPtr.Zero : userData);
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

    private static uint NormalizeTimeout(TimeSpan timeout)
    {
        if (timeout <= TimeSpan.Zero)
            return 0;
        double millis = timeout.TotalMilliseconds;
        if (millis <= 1)
            return 1;
        if (millis >= uint.MaxValue)
            return uint.MaxValue;
        return (uint)millis;
    }

    private static uint NormalizeRequestTimeout(TimeSpan timeout)
    {
        TimeSpan effective = timeout <= TimeSpan.Zero ? DefaultRequestTimeout : timeout;
        double millis = effective.TotalMilliseconds;
        if (millis <= 1)
            return 1;
        if (millis >= uint.MaxValue)
            return uint.MaxValue;
        return (uint)millis;
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

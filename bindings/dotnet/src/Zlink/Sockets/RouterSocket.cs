// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public sealed class RouterSocket : ConnectableRoutedMessageSocketBase
{
    private static readonly TimeSpan DefaultRequestTimeout = TimeSpan.FromSeconds(5);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RequestReplyHandler =
        OnRequestReply;
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate SpotReplyHandler =
        OnSpotReply;
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

    public void Request(RoutingId peerRid, Message part,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => Request(peerRid, new[] { part }, callback, flags, timeout);

    public void Request(RoutingId peerRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestReplySupport.AttachResultCallback(
            () => RequestAsyncCore(peerRid, parts, timeout ?? TimeSpan.Zero,
                CancellationToken.None, (int)flags),
            (result, reply) =>
            {
                IReadOnlyList<Message> payload = Array.Empty<Message>();
                if (reply != null)
                {
                    Received copy = RequestReplySupport.CloneReceived(reply);
                    reply.Dispose();
                    payload = copy.Parts;
                }
                callback(result, payload);
            });

    public void Reply(RoutingId peerRid, ulong requestSequence, Message message,
        SendFlags flags = SendFlags.None)
        => Reply(peerRid, requestSequence, new[] { message }, flags);

    public unsafe void Reply(RoutingId peerRid, ulong requestSequence,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        byte[] ridBytes = RoutingIdCodec.FromRoutingId(peerRid);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(ridBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        try
        {
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_router_reply(Handle,
                    ref nativeRoutingId, requestSequence, (IntPtr)nativePtr,
                    (nuint)nativeParts.Length);
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            RequestReplySupport.RestoreManagedParts(cloned, nativeParts,
                nativeParts.Length);
            throw;
        }
    }

    public void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, SendFlags flags = SendFlags.None)
        => SendToSpot(destNodeRid, destSpotRid, new[] { message }, flags);

    public unsafe void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        EnsureParts(parts, nameof(parts));
        byte[] nodeRidBytes = RoutingIdCodec.FromRoutingId(destNodeRid);
        byte[] spotRidBytes = RoutingIdCodec.FromRoutingId(destSpotRid);
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(nodeRidBytes);
        ZlinkRoutingId spotRid = NativeHelpers.WriteRoutingId(spotRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        try
        {
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_router_send_spot(Handle, ref nodeRid,
                    ref spotRid, (IntPtr)nativePtr, (nuint)nativeParts.Length,
                    (int)flags);
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            RequestReplySupport.RestoreManagedParts(cloned, nativeParts,
                nativeParts.Length);
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

    public void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan timeout = default)
        => RequestToSpot(destNodeRid, destSpotRid, new[] { message }, callback,
            flags, timeout);

    public void RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan timeout = default)
        => RequestReplySupport.AttachResultCallback(
            () => RequestToSpotAsyncInternal(destNodeRid, destSpotRid, parts,
                timeout, CancellationToken.None, (int)flags),
            (result, reply) =>
            {
                IReadOnlyList<Message> payload = Array.Empty<Message>();
                if (reply != null)
                {
                    Received copy = RequestReplySupport.CloneReceived(reply);
                    reply.Dispose();
                    payload = copy.Parts;
                }
                callback(result, payload);
            });

    public void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSequence, Message message, SendFlags flags = SendFlags.None)
        => ReplyToSpot(destNodeRid, destSpotRid, requestSequence, new[] { message },
            flags);

    public unsafe void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSequence, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        byte[] nodeRidBytes = RoutingIdCodec.FromRoutingId(destNodeRid);
        byte[] spotRidBytes = RoutingIdCodec.FromRoutingId(destSpotRid);
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(nodeRidBytes);
        ZlinkRoutingId spotRid = NativeHelpers.WriteRoutingId(spotRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        try
        {
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_router_reply_spot(Handle,
                    ref nodeRid, ref spotRid, requestSequence,
                    (IntPtr)nativePtr, (nuint)nativeParts.Length);
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            RequestReplySupport.RestoreManagedParts(cloned, nativeParts,
                nativeParts.Length);
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
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
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
                    RequestCallState callbackState =
                        (RequestCallState)((GCHandle)userdata!).Target!;
                    callbackState.TrySetCanceled(CancellationToken.None);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState callbackState =
                    (RequestCallState)((GCHandle)userdata!).Target!;
                callbackState.TrySetException(
                    new ZlinkRequestException(RequestResult.TimedOut));
            }, handle, (int)timeoutMs, Timeout.Infinite));

            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_router_request_spot(Handle,
                    ref nodeRid, ref spotRid, (IntPtr)nativePtr,
                    (nuint)nativeParts.Length, SpotReplyHandler,
                    GCHandle.ToIntPtr(handle), flags, timeoutMs);
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
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

    private unsafe Task<Received> RequestAsyncCore(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        byte[] ridBytes = RoutingIdCodec.FromRoutingId(peerRid);
        ZlinkRoutingId nativeRoutingId = NativeHelpers.WriteRoutingId(ridBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
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
                    RequestCallState callbackState =
                        (RequestCallState)((GCHandle)userdata!).Target!;
                    callbackState.TrySetCanceled(CancellationToken.None);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState callbackState =
                    (RequestCallState)((GCHandle)userdata!).Target!;
                callbackState.TrySetException(new ZlinkRequestException(
                    RequestResult.TimedOut, (int)ErrorCode.ETimedOut));
            }, handle, (int)timeoutMs, Timeout.Infinite));

            IntPtr userData = GCHandle.ToIntPtr(handle);
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_router_request(Handle,
                    ref nativeRoutingId, (IntPtr)nativePtr,
                    (nuint)nativeParts.Length, RequestReplyHandler, userData,
                    flags, timeoutMs);
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
            Received received = new(null, replyParts);
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
            Received received = new(null, replyParts);
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

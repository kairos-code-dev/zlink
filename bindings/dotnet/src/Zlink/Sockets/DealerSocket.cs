// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public sealed class DealerSocket : MessageSocketBase
{
    private static readonly TimeSpan DefaultRequestTimeout = TimeSpan.FromSeconds(5);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RequestReplyHandler =
        OnRequestReply;

    public DealerSocketOptions DealerOptions { get; }

    public DealerSocket(Context context)
        : base(context, SocketType.Dealer)
    {
        DealerOptions = new DealerSocketOptions(this);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        Kernel.SetOption(SocketOptions.RoutingId, routingId.ToBytes());
    }

    public RoutingId GetRoutingId()
    {
        return RoutingId.FromBytes(Kernel.GetOption(SocketOptions.RoutingId));
    }

    public void AttachDiscovery(Discovery discovery)
    {
        Kernel.AttachDiscovery(discovery);
    }

    public Task<IReadOnlyList<Message>> RequestAsync(Message part,
        CancellationToken ct = default)
        => RequestAsync(new[] { part }, ct);

    public Task<IReadOnlyList<Message>> RequestAsync(Message part,
        TimeSpan timeout, CancellationToken ct = default)
        => RequestAsync(new[] { part }, timeout, ct);

    public async Task<IReadOnlyList<Message>> RequestAsync(
        IReadOnlyList<Message> parts, CancellationToken ct = default)
    {
        Received received = await RequestAsyncCore(parts, DefaultRequestTimeout, ct)
            .ConfigureAwait(false);
        return received.Parts;
    }

    public async Task<IReadOnlyList<Message>> RequestAsync(
        IReadOnlyList<Message> parts, TimeSpan timeout,
        CancellationToken ct = default)
    {
        Received received = await RequestAsyncCore(parts, timeout, ct)
            .ConfigureAwait(false);
        return received.Parts;
    }

    public void Request(Message part,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => Request(new[] { part }, callback, flags, timeout);

    public void Request(IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestReplySupport.AttachResultCallback(
            () => RequestAsyncCore(parts, timeout ?? TimeSpan.Zero,
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

    private unsafe Task<Received> RequestAsyncCore(IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, int flags = 0)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));

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
            IntPtr userData = GCHandle.ToIntPtr(handle);

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

            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_dealer_request(Handle,
                    (IntPtr)nativePtr, (nuint)nativeParts.Length,
                    RequestReplyHandler, userData, (int)flags, timeoutMs);
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

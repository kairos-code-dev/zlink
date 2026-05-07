// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class DealerSocket : MessageSocketBase
{
    private static readonly TimeSpan DefaultRequestTimeout = TimeSpan.FromSeconds(5);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RequestReplyHandler =
        OnRequestReply;
    private static readonly IntPtr RequestReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RequestReplyHandler);

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

    public new void SetChannelName(string channelName)
    {
        SetChannelNameCore(channelName);
    }

    public new string GetChannelName()
    {
        return GetChannelNameCore();
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

    public bool Request(Message part,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => Request(part, callback, SendFlags.None, timeout);

    public bool Request(IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => Request(parts, callback, SendFlags.None, timeout);

    public bool Request(Message part,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
        => Request(new[] { part }, callback, flags, timeout);

    public bool Request(IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
    {
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestAsyncCore(parts, timeout ?? TimeSpan.Zero,
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

    private unsafe Task<Received> RequestAsyncCore(IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, int flags = 0)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));

        Message[] cloned = RequestReplySupport.CloneParts(parts);
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
                    RequestCallState.CancelFromUserData(userdata);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState.RequestTimeoutFromUserData(userdata);
            }, handle, (int)timeoutMs, Timeout.Infinite));

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_dealer_request_part(Handle,
                        ref nativePart, (int)flags,
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

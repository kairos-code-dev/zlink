// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal sealed class DealerSocket : MessageSocketBase, IDealerSocket
{
    private static readonly TimeSpan DefaultRequestTimeout = TimeSpan.FromSeconds(5);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RequestReplyHandler =
        OnRequestReply;
    private static readonly IntPtr RequestReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RequestReplyHandler);

    public new DealerSocketOptions Options { get; }

    public DealerSocket(Context context)
        : base(context, SocketType.Dealer)
    {
        Options = new DealerSocketOptions(this);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        Kernel.SetOption(SocketOptions.RoutingId, routingId.ToBytes());
    }

    public RoutingId GetRoutingId()
    {
        return RoutingId.From(Kernel.GetOption(SocketOptions.RoutingId));
    }

    public void AttachDiscovery(IDiscovery discovery)
    {
        Kernel.AttachDiscovery(SocketInterop.RequireDiscovery(discovery,
            nameof(discovery)));
    }

    public new void SetChannelName(string channelName)
    {
        SetChannelNameCore(channelName);
    }

    public new string GetChannelName()
    {
        return GetChannelNameCore();
    }

    /// <summary>
    /// Start a dealer request (operation builder).
    /// </summary>
    public RequestOperation Request()
    {
        return new DealerRequestOperation(this);
    }

    public override bool Recv(Received result, RecvFlags flags = RecvFlags.None)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));

        List<Message> parts = new();
        ReceivedMessageType messageType = ReceivedMessageType.Raw;
        ulong requestSeq = 0;
        bool firstPart = true;
        bool transferred = false;

        try
        {
            while (true)
            {
                ZlinkMsg nativePart = default;
                int initRc = NativeMethods.zlink_msg_init(ref nativePart);
                if (initRc != 0)
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());

                bool ownsNativePart = true;
                int recvFlags = firstPart ? (int)flags : 0;
                try
                {
                    int rc = NativeMethods.zlink_dealer_recv_part(Handle,
                        out byte nativeMessageType, out ulong nativeRequestSeq,
                        ref nativePart, out NativeMethods.ZlinkPartFlag hasMore,
                        recvFlags);

                    if (rc != 0)
                    {
                        int errno = NativeMethods.zlink_errno();
                        if (firstPart && (flags & RecvFlags.DontWait) != 0
                            && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                                or ErrorCode.EBusy)
                        {
                            return false;
                        }

                        throw ZlinkException.CreateRecvException(errno);
                    }

                    messageType = (ReceivedMessageType)nativeMessageType;
                    requestSeq = nativeRequestSeq;
                    parts.Add(Message.AdoptNative(ref nativePart));
                    ownsNativePart = false;
                    firstPart = false;

                    if (hasMore == NativeMethods.ZlinkPartFlag.Final)
                        break;
                }
                finally
                {
                    if (ownsNativePart)
                        NativeMethods.zlink_msg_close(ref nativePart);
                }
            }

            result.PopulateMessageEnvelope(parts.ToArray(), messageType,
                requestSeq == 0 ? null : requestSeq, replyHandler: null);
            transferred = true;
            return true;
        }
        catch
        {
            if (!transferred)
                RequestReplySupport.DisposeParts(parts);
            throw;
        }
    }

    internal async Task<IReadOnlyList<Message>> RequestCore(
        IReadOnlyList<Message> parts, TimeSpan timeout,
        CancellationToken ct = default)
    {
        Received received = await RequestAsyncCore(parts,
            timeout == TimeSpan.Zero ? DefaultRequestTimeout : timeout, ct)
            .ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestCallbackCore(IReadOnlyList<Message> parts,
        RequestCallback callback, SendFlags flags = SendFlags.None,
        TimeSpan? timeout = null)
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

            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_dealer_request_part(Handle,
                        ref nativePart, (int)flags, partFlag, timeoutMs,
                        RequestReplyHandlerPtr, userData));

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
        TimeSpan effective = timeout == TimeSpan.Zero
            ? DefaultRequestTimeout
            : timeout;
        return BoundaryValidation.EncodeTimeoutMilliseconds(effective,
            nameof(timeout));
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

// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RoutedReplyHandler =
        OnRoutedReply;
    private static readonly IntPtr RoutedReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RoutedReplyHandler);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RoutedReplyCallbackHandler =
        OnRoutedReplyCallback;
    private static readonly IntPtr RoutedReplyCallbackHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RoutedReplyCallbackHandler);
    [ThreadStatic]
    private static RoutedPartRoutingIdCache? t_routedPartRoutingIdCache;
    private const int StackPublishPartLimit = 8;
    private const int TopicBufferSize = 256;
    private const int DontWaitFlag = 1;
    private const int ErrnoEAgain = 11;
    private const int ErrnoEWouldBlockWin = 10035;
    private const int ErrnoENotConn = 107;
    private const int ErrnoENotConnWin = 10057;
    private const int ErrnoEHostUnreach = 113;
    private const int ErrnoEHostUnreachWin = 10065;
    private const int ErrnoETimedOut = 110;
    private const int ErrnoETimedOutWin = 10060;
    private IntPtr _handle;
    private readonly SpotNode _node;
    private readonly bool _ownsHandle;
    private Action? _sendReadyHandler;
    private SpotDispatchHandler? _dispatchEventHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    private NativeMethods.ZlinkSpotDispatchEventHandlerDelegate? _dispatchEventHandlerNative;
    private bool _dispatchEventHandlerInstalled;
    private string? _publishTopicCacheKey;
    private byte[]? _publishTopicCacheUtf8;

    internal IntPtr Handle => _handle;
    internal SpotOptions Options { get; }

    internal Spot(SpotNode node)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        if (node.Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(node));
        _node = node;
        _handle = NativeMethods.zlink_spot_new(node.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _ownsHandle = true;
        Options = new SpotOptions(this);
    }

    internal Spot(SpotNode node, IntPtr handle, bool ownsHandle)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Spot handle must not be zero.",
                nameof(handle));
        _node = node;
        _handle = handle;
        _ownsHandle = ownsHandle;
        Options = new SpotOptions(this);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        EnsureNotDisposed();
        byte[] routingIdBytes = routingId.ToByteArray();
        unsafe
        {
            fixed (byte* routingIdPtr = routingIdBytes)
            {
                int rc = NativeMethods.zlink_set_routing_id(_handle,
                    (IntPtr)routingIdPtr, (nuint)routingIdBytes.Length);
                ZlinkException.ThrowConfigIfError(rc);
            }
        }
    }

    public RoutingId RoutingId
    {
        get
        {
            EnsureNotDisposed();
            int rc = NativeMethods.zlink_get_routing_id(_handle,
                out ZlinkRoutingId routingId);
            ZlinkException.ThrowConfigIfError(rc);
            return RoutingId.From(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
    }

    internal unsafe void SetOption(SpotOption option, int value)
    {
        EnsureNotDisposed();
        int local = value;
        int rc = NativeMethods.zlink_set_spot_option(_handle, option,
            (IntPtr)(&local), (nuint)sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    internal unsafe int GetOption(SpotOption option)
    {
        EnsureNotDisposed();
        int value = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_spot_option(_handle, option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    internal unsafe void SetSocketOption(SocketOptionKey<int> option, int value)
    {
        EnsureNotDisposed();
        int local = value;
        int rc = NativeMethods.zlink_set_option(_handle, (int)option.Option,
            (IntPtr)(&local), (nuint)sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    internal unsafe int GetSocketOption(SocketOptionKey<int> option)
    {
        EnsureNotDisposed();
        int value = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_option(_handle, (int)option.Option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    public TimeSpan? RequestTimeout
    {
        get => Options.RequestTimeout;
        set => Options.RequestTimeout = value;
    }

    public int SendHighWaterMark
    {
        get => Options.SendHighWaterMark;
        set => Options.SendHighWaterMark = value;
    }

    public int ReceiveHighWaterMark
    {
        get => Options.ReceiveHighWaterMark;
        set => Options.ReceiveHighWaterMark = value;
    }

    public int SendBufferSize
    {
        get => Options.SendBufferSize;
        set => Options.SendBufferSize = value;
    }

    public int ReceiveBufferSize
    {
        get => Options.ReceiveBufferSize;
        set => Options.ReceiveBufferSize = value;
    }

    public TimeSpan? SendTimeout
    {
        get => Options.SendTimeout;
        set => Options.SendTimeout = value;
    }

    public TimeSpan? ReceiveTimeout
    {
        get => Options.ReceiveTimeout;
        set => Options.ReceiveTimeout = value;
    }

    public TimeSpan? Linger
    {
        get => Options.Linger;
        set => Options.Linger = value;
    }

    public SendOperation Publish(string topic)
        => new SpotSendOperation(this, SpotOperationKind.Publish,
            topicOrChannel: topic);

    public SendOperation SendToChannel(string channelName)
        => new SpotSendOperation(this, SpotOperationKind.SendToChannel,
            topicOrChannel: channelName);

    public RequestOperation RequestToChannel(string channelName)
        => new SpotRequestOperation(this, SpotOperationKind.RequestToChannel,
            channelName: channelName);

    public SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid)
        => new SpotSendOperation(this, SpotOperationKind.SendToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid);

    public RequestOperation RequestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid)
        => new SpotRequestOperation(this, SpotOperationKind.RequestToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid);

    public RequestOperation RequestToRouter(RoutingId peerRid)
        => new SpotRequestOperation(this, SpotOperationKind.RequestToRouter,
            peerRid: peerRid);

    public ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq)
        => new SpotReplyOperation(this, SpotOperationKind.ReplyToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid,
            requestSeq: requestSeq);

    public ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq)
        => new SpotReplyOperation(this, SpotOperationKind.ReplyToRouter,
            peerRid: peerRid, requestSeq: requestSeq);

    internal bool Publish(string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        byte[] topicUtf8 = GetValidatedPublishTopicUtf8(topic,
            nameof(topic));
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(PublishNoWaitSingleCore(topicUtf8,
                message));
        }

        PublishSingleCore(topicUtf8, message, (int)flags);
        return true;
    }

    internal SendResult PublishNoWaitResult(string topic, Message message)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        return PublishNoWaitSingleCore(GetValidatedPublishTopicUtf8(topic,
            nameof(topic)), message);
    }

    internal bool Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(PublishNoWaitResult(topic,
                parts));
        }

        if (parts is Message[] array)
        {
            PublishPartsWithFlags(topic, array, (int)flags, nameof(parts));
            return true;
        }

        if (parts is List<Message> list)
        {
            PublishPartsWithFlags(topic, list, (int)flags, nameof(parts));
            return true;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishPartsWithFlags(topic, copied, (int)flags, nameof(parts));
        return true;
    }

    internal SendResult PublishNoWaitResult(string topic,
        IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if (parts is Message[] array)
            return PublishNoWaitParts(topic, array, nameof(parts));

        if (parts is List<Message> list)
            return PublishNoWaitParts(topic, list, nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitParts(topic, copied, nameof(parts));
    }

    internal bool SendToChannel(string channelName, Message message,
        SendFlags flags = SendFlags.None)
    {
        ValidateChannelName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        try
        {
            SubmitSingleChannel(channelName, message, (int)flags,
                mapNoWaitResult: false);
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal bool SendToChannel(string channelName, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateChannelName(channelName, nameof(channelName));
        EnsureNotDisposed();
        EnsureParts(parts, nameof(parts));

        try
        {
            if (parts is Message[] array)
            {
                SendToChannelCore(channelName, array, (int)flags,
                    nameof(parts));
                return true;
            }

            if (parts is List<Message> list)
            {
                SendToChannelCore(channelName, CollectionsMarshal.AsSpan(list),
                    (int)flags, nameof(parts));
                return true;
            }

            Message[] copied = new Message[parts.Count];
            for (int i = 0; i < copied.Length; i++)
                copied[i] = parts[i];
            SendToChannelCore(channelName, copied.AsSpan(), (int)flags,
                nameof(parts));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal async Task<IReadOnlyList<Message>> RequestToChannelAsync(string channelName,
        Message message, TimeSpan timeout = default, CancellationToken ct = default)
    {
        ValidateChannelName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        Received received = await RequestToChannelAsyncInternal(channelName,
            new[] { message }, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal async Task<IReadOnlyList<Message>> RequestToChannelAsync(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        ValidateChannelName(channelName, nameof(channelName));
        Received received = await RequestToChannelAsyncInternal(channelName,
            parts, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => RequestToChannel(channelName, message, callback, SendFlags.None, timeout);

    internal bool RequestToChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => RequestToChannel(channelName, parts, callback, SendFlags.None, timeout);

    internal bool RequestToChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
        => RequestToChannel(channelName, new[] { message }, callback, flags,
            timeout);

    internal bool RequestToChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
    {
        ValidateChannelName(channelName, nameof(channelName));
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestToChannelAsyncInternal(channelName, parts,
                    timeout ?? TimeSpan.Zero,
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

    public void SetSubscription(string topicOrPattern)
    {
        ValidateTopicId(topicOrPattern, nameof(topicOrPattern));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_subscription(_handle, topicOrPattern);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        ValidateTopicId(topicOrPattern, nameof(topicOrPattern));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_unset_subscription(_handle,
            topicOrPattern);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public SubscriptionEntry? SubscriptionAt(int index)
    {
        EnsureNotDisposed();
        return SubscriptionIntrospection.At(_handle, index);
    }

    internal bool SubscribePart(Message result, Span<byte> topicBuffer,
        out int topicLength, out bool hasMore,
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        if (topicBuffer.IsEmpty)
            throw new ArgumentException("Topic buffer must not be empty.",
                nameof(topicBuffer));
        return SubscribePartInto(result, topicBuffer, out topicLength,
            out hasMore, (int)flags);
    }

    public bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        try
        {
            return SubscribeInto(result, (int)flags);
        }
        catch (ZlinkException ex) when ((flags & RecvFlags.DontWait) != 0
            && ZlinkException.MapErrorCode(ex.InternalErrno) is ErrorCode.EAgain
                or ErrorCode.EBusy)
        {
            return false;
        }
    }

    internal bool SubscribeNoWait(TopicMessage result)
    {
        return Subscribe(result, RecvFlags.DontWait);
    }

    public bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        return ReceiveSubscriptionEventInto(result, (int)flags);
    }

    internal int? TryReceiveRawSubscribedFrame(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        EnsureNotDisposed();
        try
        {
            return ReceiveRawSubscribedFrameCore(destination, flags,
                out pendingFrames);
        }
        catch (ZlinkException ex) when (ZlinkException.MapErrorCode(ex.InternalErrno)
            == ErrorCode.EAgain)
        {
            pendingFrames = Array.Empty<byte[]>();
            return null;
        }
    }

    public void SetSendReadyHandler(SpotSendReadyHandler handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        SynchronizationContext? context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        int rc = NativeMethods.zlink_send_ready_handler(_handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowHandlerIfError(rc);
        _sendReadyHandler = () => handler();
        _sendReadyHandlerContext = context;
        _sendReadyHandlerNative = native;
    }

    internal void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, Message message, SendFlags flags = SendFlags.None)
        => ReplyToSpot(destNodeRid, destSpotRid, requestSeq, new[] { message },
            flags);

    internal Task<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout = default, CancellationToken ct = default)
        => RequestToSpotAsync(destNodeRid, destSpotRid, new[] { message },
            timeout, ct);

    internal async Task<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout = default, CancellationToken ct = default)
    {
        Received received = await RequestToSpotAsyncInternal(destNodeRid,
            destSpotRid, parts, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestToSpot(destNodeRid, destSpotRid, new[] { message }, callback,
            flags, timeout);

    internal bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            return RequestToSpotCallbackInternal(destNodeRid, destSpotRid, parts,
                callback, flags, timeout ?? TimeSpan.Zero);
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        try
        {
            int rc = SubmitCopiedSpotSendSingle(ref nodeRid, ref spotRid,
                message, (int)flags);
            if (rc == 0)
                return true;
            throw ZlinkException.CreateSubmitException(NativeMethods.zlink_errno());
        }
        catch (ZlinkException ex) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(ex)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal unsafe bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
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
                    NativeMethods.zlink_spot_send_spot_part(_handle,
                        ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                        partFlag));
            return true;
        }
        catch (ZlinkException ex) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(ex)
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

    private unsafe int SubmitCopiedSpotSendSingle(
        ref ZlinkRoutingId nodeRid, ref ZlinkRoutingId spotRid, Message source,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        source.CopyTo(ref nativePart);
        try
        {
            int rc = NativeMethods.zlink_spot_send_spot_part(_handle,
                ref nodeRid, ref spotRid, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            return rc;
        }
        finally
        {
            if (!submitted)
                NativeMethods.zlink_msg_close(ref nativePart);
        }
    }

    internal unsafe void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, IReadOnlyList<Message> parts,
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
                    NativeMethods.zlink_spot_reply_spot_part(_handle,
                        ref nodeRid, ref spotRid, requestSeq, ref nativePart,
                        partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    internal void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        Message message, SendFlags flags = SendFlags.None)
        => ReplyToRouter(peerRid, requestSeq, new[] { message }, flags);

    internal Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        Message message, TimeSpan timeout = default,
        CancellationToken ct = default)
        => RequestToRouterAsync(peerRid, new[] { message }, timeout, ct);

    internal async Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        Received received = await RequestToRouterAsyncInternal(peerRid, parts,
            timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToRouter(RoutingId peerRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestToRouter(peerRid, new[] { message }, callback, flags,
            timeout);

    internal bool RequestToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestToRouterAsyncInternal(peerRid, parts,
                    timeout ?? TimeSpan.Zero, CancellationToken.None, (int)flags),
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
            return false;
        }
    }

    internal unsafe void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId routingId = peerRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_spot_reply_router_part(_handle,
                        ref routingId, requestSeq, ref nativePart, partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    internal unsafe bool RecvRoutedPart(Message result, out RoutingId? routingId,
        out RoutingId? spotRid, out ulong? requestSeq, out bool hasMore,
        RecvFlags flags = RecvFlags.None)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        EnsureNotDisposed();

        routingId = null;
        spotRid = null;
        requestSeq = null;
        hasMore = false;

        ZlinkMsg part = default;
        int initRc = NativeMethods.zlink_msg_init(ref part);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(
                NativeMethods.zlink_errno());

        bool initialized = true;
        try
        {
            int rc = NativeMethods.zlink_spot_recv_part(_handle,
                out IntPtr sourceRoutingId, out IntPtr sourceSpotRid,
                out ulong nativeRequestSeq, ref part, out int more,
                (int)flags);
            if (rc != 0)
            {
                NativeMethods.zlink_msg_close(ref part);
                initialized = false;
                int errno = NativeMethods.zlink_errno();
                if ((flags & RecvFlags.DontWait) != 0
                    && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                        or ErrorCode.EBusy)
                {
                    return false;
                }

                throw ZlinkException.CreateRecvException(errno);
            }

            initialized = false;
            result.ReplaceNativeOwned(ref part);
            RoutedPartRoutingIdCache cache =
                t_routedPartRoutingIdCache ??= new RoutedPartRoutingIdCache();
            routingId = cache.NodeFromPointer(sourceRoutingId);
            spotRid = cache.SpotFromPointer(sourceSpotRid);
            requestSeq = nativeRequestSeq == 0 ? null : nativeRequestSeq;
            hasMore = more != 0;
            return true;
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref part);
            throw;
        }
    }

    private sealed unsafe class RoutedPartRoutingIdCache
    {
        private byte[]? _nodeBytes;
        private RoutingId? _nodeRoutingId;
        private byte[]? _spotBytes;
        private RoutingId? _spotRoutingId;

        internal RoutingId? NodeFromPointer(IntPtr routingIdPtr)
            => FromPointer(routingIdPtr, ref _nodeBytes, ref _nodeRoutingId);

        internal RoutingId? SpotFromPointer(IntPtr routingIdPtr)
            => FromPointer(routingIdPtr, ref _spotBytes, ref _spotRoutingId);

        private static RoutingId? FromPointer(IntPtr routingIdPtr,
            ref byte[]? cachedBytes, ref RoutingId? cachedRoutingId)
        {
            if (routingIdPtr == IntPtr.Zero)
                return null;

            ref ZlinkRoutingId native =
                ref *(ZlinkRoutingId*)routingIdPtr;
            int size = native.Size;
            if (size <= 0)
                return null;

            fixed (byte* source = native.Data)
            {
                byte[]? cached = cachedBytes;
                if (cached != null && cached.Length == size)
                {
                    bool same = true;
                    for (int i = 0; i < size; i++)
                    {
                        if (cached[i] != source[i])
                        {
                            same = false;
                            break;
                        }
                    }
                    if (same)
                        return cachedRoutingId;
                }

                byte[] bytes = new byte[size];
                new ReadOnlySpan<byte>(source, size).CopyTo(bytes);
                RoutingId? routingId = RoutingId.FromOwnedOptionalBytes(bytes);
                cachedBytes = bytes;
                cachedRoutingId = routingId;
                return routingId;
            }
        }
    }

    public unsafe bool RecvRouted(Received result,
        RecvFlags flags = RecvFlags.None)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        MultipartMessageCollection? parts;
        byte[]? nodeRidBytes;
        byte[]? spotRidBytes;
        ulong requestSeq;
        try
        {
            parts = ReceiveSpotRoutedParts((int)flags, out nodeRidBytes,
                out spotRidBytes, out requestSeq,
                allowNoData: (flags & RecvFlags.DontWait) != 0);
        }
        catch (ZlinkException ex) when ((flags & RecvFlags.DontWait) != 0
            && ZlinkException.MapErrorCode(ex.InternalErrno) is ErrorCode.EAgain
                or ErrorCode.EBusy)
        {
            return false;
        }
        if (parts == null)
            return false;
        RoutingId? nodeRid = nodeRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(nodeRidBytes);
        RoutingId? spotRid = spotRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(spotRidBytes);
        RoutingIdSnapshot nodeRidSnapshot = RoutingIdSnapshot.FromBytes(nodeRidBytes);
        RoutingIdSnapshot spotRidSnapshot = RoutingIdSnapshot.FromBytes(spotRidBytes);
        ReceivedReplyHandler? replyHandler = requestSeq == 0
            ? null
            : CreateRoutedReplyHandler(nodeRid, spotRid, requestSeq);
        result.PopulateRoutedMultipart(parts, nodeRidSnapshot, spotRidSnapshot,
            requestSeq == 0 ? null : requestSeq, replyHandler,
            CreateRoutedSendHandler(nodeRid, spotRid));
        return true;
    }

    public ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        IntPtr parts = IntPtr.Zero;
        nuint partCount = 0;
        int rc = NativeMethods.zlink_spot_actor_join_recv(_handle,
            out ZlinkActorJoinInfo nativeInfo, out parts, out partCount,
            (int)flags);
        if (rc != 0)
        {
            int errno = NativeMethods.zlink_errno();
            if ((flags & RecvFlags.DontWait) != 0
                && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                return null;
            throw ZlinkException.CreateRecvException(errno);
        }

        ActorJoinInfo info = ActorInterop.FromNative(ref nativeInfo);
        Message[] messages = Message.FromNativeVector(parts, partCount);
        NativeMethods.zlink_multipart_close(parts, partCount);
        return new ActorJoinRequest(info, messages, nativeInfo);
    }

    public SpotActorLifecycleEvent? RecvActorLifecycle(
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_recv_actor_lifecycle(
            _handle, out ZlinkSpotActorLifecycleEvent lifecycleEvent,
            (int)flags);
        if (rc != 0)
        {
            int errno = NativeMethods.zlink_errno();
            if ((flags & RecvFlags.DontWait) != 0
                && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                return null;
            throw ZlinkException.CreateRecvException(errno);
        }

        return new SpotActorLifecycleEvent(
            (SpotActorLifecycleEventKind)lifecycleEvent.Kind,
            ActorInterop.FromNative(ref lifecycleEvent.Info));
    }

    public ActorJoinReplyOperation ReplyActorJoin(ActorJoinRequest request,
        int joinResultCode)
    {
        if (request == null)
            throw new ArgumentNullException(nameof(request));
        return new ActorJoinReplyOperationImpl(this, request, joinResultCode);
    }

    internal void ReplyActorJoinInternal(ActorJoinRequest request, int joinResultCode,
        IReadOnlyList<Message> parts)
    {
        if (request == null)
            throw new ArgumentNullException(nameof(request));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        EnsureNotDisposed();
        ZlinkActorJoinInfo nativeInfo = request.RuntimeState is ZlinkActorJoinInfo value
            ? value
            : throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        ZlinkMsg[] nativeParts = new ZlinkMsg[cloned.Length];
        bool[] submitted = new bool[cloned.Length];
        try
        {
            for (int i = 0; i < cloned.Length; i++)
                cloned[i].MoveTo(ref nativeParts[i]);

            int rc = nativeParts.Length == 0
                ? NativeMethods.zlink_spot_actor_join_reply_empty(_handle,
                    ref nativeInfo, joinResultCode, IntPtr.Zero, 0)
                : NativeMethods.zlink_spot_actor_join_reply(_handle,
                    ref nativeInfo, joinResultCode, ref nativeParts[0],
                    (nuint)nativeParts.Length);
            Array.Fill(submitted, true);
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        finally
        {
            for (int i = 0; i < nativeParts.Length; i++)
            {
                if (!submitted[i])
                    NativeMethods.zlink_msg_close(ref nativeParts[i]);
            }
            foreach (Message message in cloned)
                message.Dispose();
        }
    }

    public ActorRef[] Actors()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_actors(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<ActorRef>();

        int entrySize = Marshal.SizeOf<ZlinkActorRef>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_actors(_handle, entries,
                ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            ActorRef[] result = new ActorRef[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                ZlinkActorRef native =
                    Marshal.PtrToStructure<ZlinkActorRef>(
                        IntPtr.Add(entries, i * entrySize));
                result[i] = ActorInterop.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    public unsafe void SetDispatchHandler(SpotDispatchHandler handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        _dispatchEventHandler = handler;
        EnsureDispatchEventHandlerInstalled();
    }

    private unsafe void EnsureDispatchEventHandlerInstalled()
    {
        EnsureNotDisposed();
        if (_dispatchEventHandlerInstalled)
            return;
        _dispatchEventHandlerNative = OnNativeDispatchEvent;
        int rc = NativeMethods.zlink_spot_dispatch_event_handler(_handle,
            _dispatchEventHandlerNative, IntPtr.Zero);
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
        _dispatchEventHandlerInstalled = true;
    }

    internal void DrainChannelReplyFrom(IntPtr dealerSubject)
    {
        EnsureNotDisposed();
        if (dealerSubject == IntPtr.Zero)
            throw new ArgumentException("dealerSubject must not be null.",
                nameof(dealerSubject));
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        Destroy(throwOnError: true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Spot()
    {
        Destroy(throwOnError: false);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = _ownsHandle
            ? NativeMethods.zlink_spot_destroy(ref handle)
            : 0;
        if (rc != 0)
        {
            _handle = originalHandle;
            if (throwOnError)
            {
                throw ZlinkException.CreateCloseException(
                    NativeMethods.zlink_errno());
            }
            return;
        }

        _handle = IntPtr.Zero;
        _sendReadyHandler = null;
        _dispatchEventHandler = null;
        _sendReadyHandlerContext = null;
        _sendReadyHandlerNative = null;
        _dispatchEventHandlerNative = null;
        _dispatchEventHandlerInstalled = false;
        _node.UnregisterSpot(this);
    }

    private void PublishSingleCore(string topic, Message message,
        int flags)
    {
        PublishSingleCore(GetPublishTopicUtf8(topic), message, flags);
    }

    private unsafe void PublishSingleCore(byte[] topicUtf8, Message message,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            message.MoveTo(ref nativePart);
            fixed (byte* topicPtr = topicUtf8)
            {
                int rc = NativeMethods.zlink_spot_publish_part_utf8(_handle,
                    topicPtr, ref nativePart, (int)flags,
                    NativeMethods.ZlinkPartFlag.Final);
                submitted = true;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            if (!submitted)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Spot));
    }

    private void OnNativeSendReady(IntPtr subject, IntPtr userData)
    {
        Action? handler = _sendReadyHandler;
        SynchronizationContext? context = _sendReadyHandlerContext;
        if (handler == null)
            return;

        try
        {
            CallbackDelivery.Post(context, handler);
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
        }
    }

    private unsafe Task<Received> RequestToChannelAsyncInternal(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
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
                    int rc = NativeMethods.zlink_spot_request_channel_part(_handle,
                        channelName, ref nativePart,
                        RoutedReplyHandlerPtr,
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

            return RequestProgressPump.AttachSpot(_handle, completion.Task);
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            if (handle.IsAllocated)
                handle.Free();
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
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_spot_part(_handle,
                    ref nodeRid, ref spotRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }

    private unsafe bool RequestToSpotCallbackInternal(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback, SendFlags flags,
        TimeSpan timeout)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        uint timeoutMs = NormalizeTimeout(timeout);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        GCHandle handle = default;
        SpotRequestCallbackState? state = null;

        try
        {
            state = new SpotRequestCallbackState(callback,
                RequestProgressPump.AttachSpotCallback(_handle));
            handle = GCHandle.Alloc(state, GCHandleType.Normal);

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_spot_request_spot_part(_handle,
                        ref nodeRid, ref spotRid, ref nativePart,
                        RoutedReplyCallbackHandlerPtr, GCHandle.ToIntPtr(handle),
                        (int)flags,
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

            return true;
        }
        catch
        {
            state?.DisposeProgress();
            if (handle.IsAllocated)
                handle.Free();
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe Task<Received> RequestToRouterAsyncInternal(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nativePeerRid = peerRid.ToNative();
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_router_part(_handle,
                    ref nativePeerRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }

    private unsafe delegate int SpotRequestPartSubmitter(
        ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
        NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs);

    private unsafe Task<Received> RequestRoutedAsyncInternal(
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags, SpotRequestPartSubmitter submit)
    {
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
                    int rc = submit(ref nativePart,
                        RoutedReplyHandlerPtr,
                        GCHandle.ToIntPtr(handle),
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

            return RequestProgressPump.AttachSpot(_handle, completion.Task);
        }
        catch
        {
            if (state != null)
                state.Dispose();
            if (handle.IsAllocated)
                handle.Free();
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private static void OnRoutedReply(int result, IntPtr parts, nuint partCount,
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

    private static uint NormalizeTimeout(TimeSpan timeout)
    {
        return BoundaryValidation.EncodeTimeoutMilliseconds(timeout,
            nameof(timeout));
    }

    private static void EnsureParts(IReadOnlyList<Message> parts, string paramName)
    {
        if (parts == null)
            throw new ArgumentNullException(paramName);
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", paramName);
    }

    private static void ValidateTopicId(string value, string paramName)
    {
        BoundaryValidation.ValidateTopicOrFilterUtf8(value, paramName,
            allowEmpty: false);
    }

    private byte[] GetPublishTopicUtf8(string topic)
    {
        byte[]? cached = _publishTopicCacheUtf8;
        string? cachedKey = _publishTopicCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, topic)
                || string.Equals(cachedKey, topic, StringComparison.Ordinal)))
        {
            return cached;
        }

        byte[] encoded = PublishTopicEncoding.GetNullTerminatedUtf8(topic);
        _publishTopicCacheKey = topic;
        _publishTopicCacheUtf8 = encoded;
        return encoded;
    }

    private byte[] GetValidatedPublishTopicUtf8(string topic, string paramName)
    {
        byte[]? cached = _publishTopicCacheUtf8;
        string? cachedKey = _publishTopicCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, topic)
                || string.Equals(cachedKey, topic, StringComparison.Ordinal)))
        {
            return cached;
        }

        ValidateTopicId(topic, paramName);
        return GetPublishTopicUtf8(topic);
    }

    private static void ValidateChannelName(string value, string paramName)
    {
        BoundaryValidation.ValidateFixedUtf8(value, paramName);
    }

    private void PublishPartsWithFlags(string topic, IReadOnlyList<Message> parts,
        int flags, string paramName)
    {
        if (parts is Message[] array)
        {
            PublishCore(topic, array.AsSpan(), flags, paramName);
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(topic, CollectionsMarshal.AsSpan(list), flags,
                paramName);
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(topic, copied.AsSpan(), flags, paramName);
    }

    private SendResult PublishNoWaitParts(string topic,
        IReadOnlyList<Message> parts, string paramName)
    {
        if (parts is Message[] array)
            return PublishNoWaitCore(topic, array.AsSpan(), paramName);

        if (parts is List<Message> list)
            return PublishNoWaitCore(topic, CollectionsMarshal.AsSpan(list),
                paramName);

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitCore(topic, copied.AsSpan(), paramName);
    }

    private static T? TryReceiveCore<T>(Func<T> operation) where T : class
    {
        try
        {
            return operation();
        }
        catch (ZlinkException ex) when (ZlinkException.MapErrorCode(
            ex.InternalErrno) is ErrorCode.EAgain or ErrorCode.EBusy)
        {
            return null;
        }
    }

    private static SendResult MapSendResult(int rc)
    {
        return rc switch
        {
            0 => SendResult.Sent,
            1 => SendResult.Backpressured,
            2 => SendResult.NotReady,
            _ => throw new InvalidOperationException(
                $"Unexpected send result code '{rc}'.")
        };
    }

    private static SendResult? TryMapSendResultFromErrno()
    {
        int errno = NativeMethods.zlink_errno();
        return errno switch
        {
            ErrnoEAgain => SendResult.Backpressured,
            ErrnoEWouldBlockWin => SendResult.Backpressured,
            ErrnoENotConn => SendResult.NotReady,
            ErrnoENotConnWin => SendResult.NotReady,
            ErrnoEHostUnreach => SendResult.NotReady,
            ErrnoEHostUnreachWin => SendResult.NotReady,
            ErrnoETimedOut => SendResult.NotReady,
            ErrnoETimedOutWin => SendResult.NotReady,
            _ => null
        };
    }

    private static unsafe int CopyFirstFrameAndCollectPending(IntPtr nativeParts,
        nuint partCount, Span<byte> destination, out byte[][] pendingFrames)
    {
        int total = checked((int)partCount);
        if (total <= 0)
        {
            pendingFrames = Array.Empty<byte[]>();
            return 0;
        }

        ZlinkMsg* src = (ZlinkMsg*)nativeParts;
        IntPtr firstPtr = new IntPtr(src);
        int firstSize = checked((int)NativeMethods.zlink_msg_size(firstPtr));
        if (firstSize > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        IntPtr firstData = NativeMethods.zlink_msg_data(firstPtr);
        if (firstSize != 0 && firstData != IntPtr.Zero)
            new ReadOnlySpan<byte>((void*)firstData, firstSize).CopyTo(destination);

        if (total == 1)
        {
            pendingFrames = Array.Empty<byte[]>();
            return firstSize;
        }

        pendingFrames = new byte[total - 1][];
        for (int i = 1; i < total; i++)
        {
            IntPtr msgPtr = new IntPtr(src + i);
            int size = checked((int)NativeMethods.zlink_msg_size(msgPtr));
            if (size == 0)
            {
                pendingFrames[i - 1] = Array.Empty<byte>();
                continue;
            }

            IntPtr dataPtr = NativeMethods.zlink_msg_data(msgPtr);
            if (dataPtr == IntPtr.Zero)
            {
                pendingFrames[i - 1] = Array.Empty<byte>();
                continue;
            }

            byte[] payload = new byte[size];
            new ReadOnlySpan<byte>((void*)dataPtr, size).CopyTo(payload);
            pendingFrames[i - 1] = payload;
        }

        return firstSize;
    }

}

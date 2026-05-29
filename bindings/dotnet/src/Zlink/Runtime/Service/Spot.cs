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
    private string? _channelNameCacheKey;
    private byte[]? _channelNameCacheUtf8;

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

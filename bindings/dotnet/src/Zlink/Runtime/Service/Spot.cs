// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    private const int TopicBufferSize = 256;
    private const int DontWaitFlag = 1;

    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RoutedReplyHandler =
        OnRoutedReply;

    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RoutedReplyCallbackHandler =
        OnRoutedReplyCallback;

    private static readonly IntPtr RoutedReplyCallbackHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RoutedReplyCallbackHandler);

    [ThreadStatic] private static RoutedPartRoutingIdCache? t_routedPartRoutingIdCache;

    private readonly SpotNode _node;
    private readonly bool _ownsHandle;
    private string? _channelNameCacheKey;
    private byte[]? _channelNameCacheUtf8;
    private SpotDispatchHandler? _dispatchEventHandler;
    private bool _dispatchEventHandlerInstalled;
    private NativeMethods.ZlinkSpotDispatchEventHandlerDelegate? _dispatchEventHandlerNative;
    private string? _publishTopicCacheKey;
    private byte[]? _publishTopicCacheUtf8;
    private Action? _sendReadyHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;

    internal Spot(SpotNode node)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        if (node.Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(node));
        _node = node;
        Handle = NativeMethods.zlink_spot_new(node.Handle);
        if (Handle == IntPtr.Zero)
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
        Handle = handle;
        _ownsHandle = ownsHandle;
        Options = new SpotOptions(this);
    }

    internal IntPtr Handle { get; private set; }

    internal static IntPtr RoutedReplyHandlerPointer { get; } =
        Marshal.GetFunctionPointerForDelegate(RoutedReplyHandler);

    internal SpotOptions Options { get; }

    public void SetSubscription(string topicOrPattern)
    {
        ValidateTopicId(topicOrPattern, nameof(topicOrPattern));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_set_subscription(Handle, topicOrPattern);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        ValidateTopicId(topicOrPattern, nameof(topicOrPattern));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_unset_subscription(Handle,
            topicOrPattern);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public SubscriptionEntry? SubscriptionAt(int index)
    {
        EnsureNotDisposed();
        return SubscriptionIntrospection.At(Handle, index);
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
                                        && ZlinkException.MapErrorCode(ex.NativeErrno) is ErrorCode.EAgain
                                            or ErrorCode.EBusy)
        {
            return false;
        }
    }

    public bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        return ReceiveSubscriptionEventInto(result, (int)flags);
    }

    public void SetSendReadyHandler(SpotSendReadyHandler handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        var context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        _sendReadyHandler = () => handler();
        _sendReadyHandlerContext = context;
        _sendReadyHandlerNative = native;
        var rc = NativeMethods.zlink_send_ready_handler(Handle, native,
            IntPtr.Zero);
        if (rc != 0)
        {
            _sendReadyHandler = null;
            _sendReadyHandlerContext = null;
            _sendReadyHandlerNative = null;
            ZlinkException.ThrowHandlerIfError(rc);
        }
    }

    public bool RecvRouted(Received result,
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
                (flags & RecvFlags.DontWait) != 0);
        }
        catch (ZlinkException ex) when ((flags & RecvFlags.DontWait) != 0
                                        && ZlinkException.MapErrorCode(ex.NativeErrno) is ErrorCode.EAgain
                                            or ErrorCode.EBusy)
        {
            return false;
        }

        if (parts == null)
            return false;
        var nodeRid = nodeRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(nodeRidBytes);
        var spotRid = spotRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(spotRidBytes);
        var nodeRidSnapshot = RoutingIdSnapshot.FromBytes(nodeRidBytes);
        var spotRidSnapshot = RoutingIdSnapshot.FromBytes(spotRidBytes);
        var replyHandler = requestSeq == 0
            ? null
            : CreateRoutedReplyHandler(nodeRid, spotRid, requestSeq);
        var sendHandler = CreateRoutedSendHandler(nodeRid,
            spotRid);
        var sendSingleHandler =
            CreateRoutedSendSingleHandler(nodeRid, spotRid);
        result.PopulateRoutedMultipart(parts, nodeRidSnapshot, spotRidSnapshot,
            requestSeq == 0 ? null : requestSeq, replyHandler, sendHandler,
            sendSingleHandler);
        return true;
    }

    public ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        var parts = IntPtr.Zero;
        nuint partCount = 0;
        var rc = NativeMethods.zlink_spot_actor_join_recv(Handle,
            out var nativeInfo, out parts, out partCount,
            (int)flags);
        if (rc != 0)
        {
            var errno = NativeMethods.zlink_errno();
            if ((flags & RecvFlags.DontWait) != 0
                && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                return null;
            throw ZlinkException.CreateRecvException(errno);
        }

        var info = ActorInterop.FromNative(ref nativeInfo);
        var messages = Message.FromNativeVector(parts, partCount);
        NativeMethods.zlink_multipart_close(parts, partCount);
        return new ActorJoinRequest(info, messages, nativeInfo);
    }

    public SpotActorLifecycleEvent? RecvActorLifecycle(
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_recv_actor_lifecycle_with_request(
            Handle, out var lifecycleEvent,
            out var parts, out var partCount, (int)flags);
        if (rc != 0)
        {
            var errno = NativeMethods.zlink_errno();
            if ((flags & RecvFlags.DontWait) != 0
                && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                return null;
            throw ZlinkException.CreateRecvException(errno);
        }

        var requestParts = Message.FromNativeVector(parts, partCount);
        return new SpotActorLifecycleEvent(
            (SpotActorLifecycleEventKind)lifecycleEvent.Kind,
            ActorInterop.FromNative(ref lifecycleEvent.Info))
        {
            RequestParts = requestParts
        };
    }

    public ActorJoinReplyOperation ReplyActorJoin(ActorJoinRequest request,
        int joinResultCode)
    {
        if (request == null)
            throw new ArgumentNullException(nameof(request));
        return new ActorJoinReplyOperationImpl(this, request, joinResultCode);
    }

    public ActorRef[] Actors()
    {
        EnsureNotDisposed();
        return NativeSnapshotReader.Read<ZlinkActorRef, ActorRef>(
            (IntPtr entries, ref nuint count) =>
                NativeMethods.zlink_spot_actors(Handle, entries, ref count),
            static (ref ZlinkActorRef native) =>
                ActorInterop.FromNative(ref native));
    }

    public void SetDispatchHandler(SpotDispatchHandler handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        _dispatchEventHandler = handler;
        EnsureDispatchEventHandlerInstalled();
    }

    public int DrainReplies()
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_drain_reply(Handle);
        if (rc < 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        return rc;
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

    internal bool RecvRoutedPart(Message result, out RoutingId? routingId,
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
        var initRc = NativeMethods.zlink_msg_init(ref part);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(
                NativeMethods.zlink_errno());

        var initialized = true;
        try
        {
            var rc = NativeMethods.zlink_spot_recv_part(Handle,
                out var sourceRoutingId, out var sourceSpotRid,
                out var nativeRequestSeq, ref part, out var more,
                (int)flags);
            if (rc != 0)
            {
                NativeMethods.zlink_msg_close(ref part);
                initialized = false;
                var errno = NativeMethods.zlink_errno();
                if ((flags & RecvFlags.DontWait) != 0
                    && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                        or ErrorCode.EBusy)
                    return false;

                throw ZlinkException.CreateRecvException(errno);
            }

            initialized = false;
            result.ReplaceNativeOwned(ref part);
            var cache =
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

    internal void ReplyActorJoinInternal(ActorJoinRequest request, int joinResultCode,
        IReadOnlyList<Message> parts)
    {
        if (request == null)
            throw new ArgumentNullException(nameof(request));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        EnsureNotDisposed();
        var nativeInfo = request.RuntimeState is ZlinkActorJoinInfo value
            ? value
            : throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
        var cloned = RequestReplySupport.CloneParts(parts);
        var nativeParts = new ZlinkMsg[cloned.Length];
        var submitted = new bool[cloned.Length];
        try
        {
            for (var i = 0; i < cloned.Length; i++)
                cloned[i].MoveTo(ref nativeParts[i]);

            var rc = nativeParts.Length == 0
                ? NativeMethods.zlink_spot_actor_join_reply_empty(Handle,
                    ref nativeInfo, joinResultCode, IntPtr.Zero, 0)
                : NativeMethods.zlink_spot_actor_join_reply(Handle,
                    ref nativeInfo, joinResultCode, ref nativeParts[0],
                    (nuint)nativeParts.Length);
            Array.Fill(submitted, true);
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        finally
        {
            for (var i = 0; i < nativeParts.Length; i++)
                if (!submitted[i])
                    NativeMethods.zlink_msg_close(ref nativeParts[i]);
            foreach (var message in cloned)
                message.Dispose();
        }
    }

    private unsafe void EnsureDispatchEventHandlerInstalled()
    {
        EnsureNotDisposed();
        if (_dispatchEventHandlerInstalled)
            return;
        _dispatchEventHandlerNative = OnNativeDispatchEvent;
        var rc = NativeMethods.zlink_spot_dispatch_event_handler(Handle,
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
        var rc = NativeMethods.zlink_spot_drain_channel_reply(Handle,
            dealerSubject);
        if (rc < 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
    }

    private sealed unsafe class RoutedPartRoutingIdCache
    {
        private byte[]? _nodeBytes;
        private RoutingId? _nodeRoutingId;
        private byte[]? _spotBytes;
        private RoutingId? _spotRoutingId;

        internal RoutingId? NodeFromPointer(IntPtr routingIdPtr)
        {
            return FromPointer(routingIdPtr, ref _nodeBytes, ref _nodeRoutingId);
        }

        internal RoutingId? SpotFromPointer(IntPtr routingIdPtr)
        {
            return FromPointer(routingIdPtr, ref _spotBytes, ref _spotRoutingId);
        }

        private static RoutingId? FromPointer(IntPtr routingIdPtr,
            ref byte[]? cachedBytes, ref RoutingId? cachedRoutingId)
        {
            if (routingIdPtr == IntPtr.Zero)
                return null;

            ref var native =
                ref *(ZlinkRoutingId*)routingIdPtr;
            int size = native.Size;
            if (size <= 0)
                return null;

            fixed (byte* source = native.Data)
            {
                var cached = cachedBytes;
                if (cached != null && cached.Length == size)
                {
                    var same = true;
                    for (var i = 0; i < size; i++)
                        if (cached[i] != source[i])
                        {
                            same = false;
                            break;
                        }

                    if (same)
                        return cachedRoutingId;
                }

                var bytes = new byte[size];
                new ReadOnlySpan<byte>(source, size).CopyTo(bytes);
                var routingId = RoutingId.FromOwnedOptionalBytes(bytes);
                cachedBytes = bytes;
                cachedRoutingId = routingId;
                return routingId;
            }
        }
    }
}

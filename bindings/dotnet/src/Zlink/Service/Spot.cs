// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Zlink;
using Zlink.Native;

namespace Zlink;

public sealed class SpotNode : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;
    public SpotNodePublisherOptions PublisherOptions { get; }
    public SpotNodeSubscriberOptions SubscriberOptions { get; }

    public SpotNode(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_spot_node_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        PublisherOptions = new SpotNodePublisherOptions(this);
        SubscriberOptions = new SpotNodeSubscriberOptions(this);
    }

    internal IntPtr Handle => _handle;

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
                ZlinkException.ThrowIfError(rc);
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
            ZlinkException.ThrowIfError(rc);
            return RoutingId.FromBytes(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
    }

    public void Bind(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_bind(_handle, endpoint);
        ZlinkException.ThrowIfError(rc);
    }

    /// <summary>
    /// Returns the resolved endpoint after bind (supports ephemeral port 0).
    /// </summary>
    public string LastEndpoint
    {
        get
        {
            return StatusSnapshot().LocalEndpoint;
        }
    }

    public void ConnectPeer(string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_peer(_handle,
            peerEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void DisconnectPeer(string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer(_handle,
            peerEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetAdmissionState(AdmissionState state)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_admission_state(_handle, (int)state);
        ZlinkException.ThrowIfError(rc);
    }

    public AdmissionState GetAdmissionState()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_get_admission_state(_handle,
            out int state);
        ZlinkException.ThrowIfError(rc);
        return (AdmissionState)state;
    }

    /// <summary>
    /// Attaches this SPOT node to a discovery-owned service lifecycle.
    /// </summary>
    /// <remarks>
    /// Once attached, the discovery instance becomes the lifecycle owner for
    /// the node and is responsible for coordinated shutdown.
    /// </remarks>
    public void AttachDiscovery(Discovery discovery)
    {
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        int rc = NativeMethods.zlink_spot_node_attach_discovery(_handle,
            discovery.Handle);
        ZlinkException.ThrowIfError(rc);
    }

    public void AttachRouter(string serviceName, RouterSocket router)
    {
        EnsureNotDisposed();
        if (router == null)
            throw new ArgumentNullException(nameof(router));
        BoundaryValidation.ValidateFixedUtf8(serviceName, nameof(serviceName));
        int rc = NativeMethods.zlink_spot_node_attach_router(_handle,
            serviceName, router.Handle);
        ZlinkException.ThrowIfError(rc);
    }

    public void AttachPubSub(string serviceName, PubSocket pub, SubSocket sub)
    {
        EnsureNotDisposed();
        if (pub == null)
            throw new ArgumentNullException(nameof(pub));
        if (sub == null)
            throw new ArgumentNullException(nameof(sub));
        BoundaryValidation.ValidateFixedUtf8(serviceName, nameof(serviceName));
        int rc = NativeMethods.zlink_spot_node_attach_pubsub(_handle,
            serviceName, pub.Handle, sub.Handle);
        ZlinkException.ThrowIfError(rc);
    }

    public Spot CreateSpot()
    {
        EnsureNotDisposed();
        return new Spot(this);
    }

    internal void SetOption(SpotNodeSocketRole role, SocketOptionKey<int> option,
        int value)
    {
        EnsureNotDisposed();
        unsafe
        {
            int local = value;
            int code = (int)option.Option;
            int rc = role switch
            {
                SpotNodeSocketRole.Node => NativeMethods.zlink_set_option(
                    _handle, code, new IntPtr(&local),
                    (nuint)sizeof(int)),
                SpotNodeSocketRole.Pub => (code & 0xFF00) == 0x3300
                    ? NativeMethods.zlink_set_pub_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int))
                    : NativeMethods.zlink_set_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int)),
                SpotNodeSocketRole.Sub => (code & 0xFF00) == 0x3400
                    ? NativeMethods.zlink_set_sub_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int))
                    : NativeMethods.zlink_set_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int)),
                _ => throw new ArgumentOutOfRangeException(nameof(role))
            };
            ZlinkException.ThrowIfError(rc);
        }
    }

    public void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false)
    {
        BoundaryValidation.ValidateFixedUtf8(certPath, nameof(certPath));
        BoundaryValidation.ValidateFixedUtf8(keyPath, nameof(keyPath));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_server(_handle, certPath, keyPath,
            requireClientCert ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        BoundaryValidation.ValidateFixedUtf8(caCertPath, nameof(caCertPath));
        BoundaryValidation.ValidateFixedUtf8(hostname, nameof(hostname));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_client(_handle, caCertPath,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public SpotNodeStatus StatusSnapshot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_status_snapshot(_handle,
            out var native);
        ZlinkException.ThrowIfError(rc);
        return SpotNodeStatus.FromNative(ref native);
    }

    public SpotNodePeerEntry[] PeersSnapshot()
    {
        EnsureNotDisposed();
        return ReadPeerEntries(IntPtr.Zero);
    }

    public int ServiceAttachmentCount()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_service_attachment_count(
            _handle, ref count);
        ZlinkException.ThrowIfError(rc);
        return checked((int)count);
    }

    public SpotServiceAttachmentStats ServiceAttachmentAt(int index)
    {
        EnsureNotDisposed();
        if (index < 0)
            throw new ArgumentOutOfRangeException(nameof(index));
        int rc = NativeMethods.zlink_spot_node_service_attachment_at(_handle,
            (nuint)index, out ZlinkSpotServiceAttachmentStats native);
        ZlinkException.ThrowIfError(rc);
        return SpotServiceAttachmentStats.FromNative(ref native);
    }

    public SpotServiceMonitorEvent NodeMonitorRecv(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_monitor_recv(_handle,
            out ZlinkSpotServiceMonitorEvent native, (int)flags);
        if (rc != 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        return SpotServiceMonitorEvent.FromNative(ref native);
    }

    public SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodePeerFilter nativeFilter = default;
            nativeFilter.Source = (int)filter.Source.GetValueOrDefault();
            nativeFilter.State = (int)filter.State.GetValueOrDefault();
            if (!string.IsNullOrEmpty(filter.PeerEndpoint))
            {
                BoundaryValidation.ValidateFixedUtf8(filter.PeerEndpoint,
                    nameof(filter.PeerEndpoint));
                WriteFixedString(filter.PeerEndpoint, nativeFilter.PeerEndpoint,
                    256);
            }

            return ReadPeerEntries((IntPtr)(&nativeFilter));
        }
    }

    public SpotNodeSubjectEntry[] SubjectsSnapshot(
        SpotNodeSubjectFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodeSubjectFilter nativeFilter = default;
            IntPtr filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                SpotNodeSubjectFilter value = filter;
                if (value.Role.HasValue || !string.IsNullOrEmpty(value.Subject)
                    || value.SubjectKind.HasValue)
                {
                    nativeFilter.Role = (int)value.Role.GetValueOrDefault();
                    nativeFilter.SubjectKind =
                        (uint)value.SubjectKind.GetValueOrDefault();
                    if (!string.IsNullOrEmpty(value.Subject))
                    {
                        BoundaryValidation.ValidateFixedUtf8(value.Subject,
                            nameof(SpotNodeSubjectFilter.Subject));
                        WriteFixedString(value.Subject, nativeFilter.Subject,
                            256);
                    }
                    filterPtr = (IntPtr)(&nativeFilter);
                }
            }

            return ReadSubjectEntries(filterPtr);
        }
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_spot_node_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~SpotNode()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SpotNode));
    }

    private SpotNodePeerEntry[] ReadPeerEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = filterPtr == IntPtr.Zero
            ? NativeMethods.zlink_spot_node_peers_snapshot(_handle, IntPtr.Zero,
                ref count)
            : NativeMethods.zlink_spot_node_peers_query(_handle, filterPtr,
                IntPtr.Zero, ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodePeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodePeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = filterPtr == IntPtr.Zero
                ? NativeMethods.zlink_spot_node_peers_snapshot(_handle, entries,
                    ref actual)
                : NativeMethods.zlink_spot_node_peers_query(_handle, filterPtr,
                    entries, ref actual);
            ZlinkException.ThrowIfError(rc);

            SpotNodePeerEntry[] result = new SpotNodePeerEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodePeerEntry native =
                    Marshal.PtrToStructure<ZlinkSpotNodePeerEntry>(current);
                result[i] = SpotNodePeerEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private SpotNodeSubjectEntry[] ReadSubjectEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_subjects_snapshot(_handle,
            filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSubjectEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSubjectEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_subjects_snapshot(_handle,
                filterPtr, entries, ref actual);
            ZlinkException.ThrowIfError(rc);

            SpotNodeSubjectEntry[] result = new SpotNodeSubjectEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodeSubjectEntry native =
                    Marshal.PtrToStructure<ZlinkSpotNodeSubjectEntry>(current);
                result[i] = SpotNodeSubjectEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private static unsafe void WriteFixedString(string value, byte* destination,
        int capacity)
    {
        byte[] encoded = Encoding.UTF8.GetBytes(value);
        if (encoded.Length >= capacity)
        {
            throw new ArgumentOutOfRangeException(nameof(value),
                "UTF-8 value exceeds native fixed buffer capacity.");
        }

        for (int i = 0; i < capacity; i++)
            destination[i] = 0;
        for (int i = 0; i < encoded.Length; i++)
            destination[i] = encoded[i];
    }

}

public sealed class Spot : IDisposable, IAsyncDisposable
{
    private static readonly NativeMethods.ZlinkFreeFnDelegate BorrowedBufferFree =
        OnBorrowedBufferFree;
    private static readonly IntPtr BorrowedBufferFreePtr =
        Marshal.GetFunctionPointerForDelegate(BorrowedBufferFree);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RoutedReplyHandler =
        OnRoutedReply;
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
    private readonly bool _ownsHandle;
    private Action? _sendReadyHandler;
    private Action<Received>? _routedReceiveHandler;
    private Action<SpotDispatchEvent>? _dispatchEventHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private SynchronizationContext? _routedReceiveHandlerContext;
    private SynchronizationContext? _dispatchEventHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    private NativeMethods.ZlinkSpotRequestHandlerDelegate? _routedReceiveHandlerNative;
    private NativeMethods.ZlinkSpotDispatchEventHandlerDelegate? _dispatchEventHandlerNative;
    internal IntPtr Handle => _handle;

    internal Spot(SpotNode node)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        if (node.Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(node));
        _handle = NativeMethods.zlink_spot_new(node.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        _ownsHandle = true;
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
                ZlinkException.ThrowIfError(rc);
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
            ZlinkException.ThrowIfError(rc);
            return RoutingId.FromBytes(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
    }

    public void SetAdmissionState(AdmissionState state)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_admission_state(_handle, (int)state);
        ZlinkException.ThrowIfError(rc);
    }

    public AdmissionState GetAdmissionState()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_get_admission_state(_handle,
            out int state);
        ZlinkException.ThrowIfError(rc);
        return (AdmissionState)state;
    }

    public void Publish(string serviceName, string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        PublishSingleCore(serviceName, topic, message, (int)flags);
    }

    internal SendResult TryPublish(string serviceName, string topic,
        Message message)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        return TryPublishSingleCore(serviceName, topic, message);
    }

    public void Publish(string serviceName, string topic,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if (parts is Message[] array)
        {
            PublishPartsWithFlags(serviceName, topic, array, (int)flags,
                nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            PublishPartsWithFlags(serviceName, topic, list, (int)flags,
                nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishPartsWithFlags(serviceName, topic, copied, (int)flags,
            nameof(parts));
    }

    internal SendResult TryPublish(string serviceName, string topic,
        IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if (parts is Message[] array)
            return TryPublishPartsWithFlags(serviceName, topic, array,
                nameof(parts));

        if (parts is List<Message> list)
            return TryPublishPartsWithFlags(serviceName, topic,
                list, nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return TryPublishPartsWithFlags(serviceName, topic, copied, nameof(parts));
    }

    public void SendService(string serviceName, Message message,
        SendFlags flags = SendFlags.None)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        SendServiceCore(serviceName, new[] { message }, (int)flags,
            nameof(message));
    }

    public void SendService(string serviceName, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateServiceName(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        EnsureParts(parts, nameof(parts));

        if (parts is Message[] array)
        {
            SendServiceCore(serviceName, array, (int)flags,
                nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            SendServiceCore(serviceName, CollectionsMarshal.AsSpan(list),
                (int)flags, nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        SendServiceCore(serviceName, copied.AsSpan(), (int)flags, nameof(parts));
    }

    public Task<IReadOnlyList<Message>> RequestServiceAsync(string serviceName,
        Message message, TimeSpan timeout = default, CancellationToken ct = default)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return RequestServiceAsyncInternal(serviceName, new[] { message }, timeout,
            ct).ContinueWith(task => task.Result.Parts, TaskScheduler.Default);
    }

    internal void PublishRawSingle(string serviceName, string topic,
        ReadOnlySpan<byte> payload, int flags)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        PublishRawSingleCore(serviceName, topic, payload, flags);
    }

    internal SendResult TryPublishRawSingle(string serviceName, string topic,
        ReadOnlySpan<byte> payload)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        return TryPublishRawSingleCore(serviceName, topic, payload);
    }

    internal void PublishBorrowedSingle(string serviceName, string topic,
        byte[] payload, int flags)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        EnsureNotDisposed();
        PublishBorrowedSingleCore(serviceName, topic, payload, flags);
    }

    internal SendResult TryPublishBorrowedSingle(string serviceName, string topic,
        byte[] payload)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        EnsureNotDisposed();
        return TryPublishBorrowedSingleCore(serviceName, topic, payload);
    }

    public void SetSubscription(string topicOrPattern)
    {
        ValidateTopicId(topicOrPattern, nameof(topicOrPattern));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_subscription(_handle, topicOrPattern);
        ZlinkException.ThrowIfError(rc);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        ValidateTopicId(topicOrPattern, nameof(topicOrPattern));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_unset_subscription(_handle,
            topicOrPattern);
        ZlinkException.ThrowIfError(rc);
    }

    public TopicMessage Subscribe(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        return SubscribeCore((int)flags);
    }

    internal bool TrySubscribe(out TopicMessage? subscribed)
    {
        EnsureNotDisposed();
        subscribed = TryReceiveCore(() => SubscribeCore(1));
        return subscribed != null;
    }

    public SubscriptionEvent ReceiveSubscriptionEvent(
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        return ReceiveSubscriptionEventCore((int)flags);
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

    public void OnSendReady(Action handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        SynchronizationContext? context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        int rc = NativeMethods.zlink_send_ready_handler(_handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
        _sendReadyHandler = handler;
        _sendReadyHandlerContext = context;
        _sendReadyHandlerNative = native;
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
                int rc = NativeMethods.zlink_spot_send_spot(_handle, ref nodeRid,
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
                int rc = NativeMethods.zlink_spot_reply_spot(_handle, ref nodeRid,
                    ref spotRid, requestSequence, (IntPtr)nativePtr,
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

    public void SendToRouter(RoutingId peerRid, Message message,
        SendFlags flags = SendFlags.None)
        => SendToRouter(peerRid, new[] { message }, flags);

    public unsafe void SendToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureParts(parts, nameof(parts));
        byte[] peerRidBytes = RoutingIdCodec.FromRoutingId(peerRid);
        ZlinkRoutingId routingId = NativeHelpers.WriteRoutingId(peerRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        try
        {
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_spot_send_router(_handle,
                    ref routingId, (IntPtr)nativePtr, (nuint)nativeParts.Length,
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

    public Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        Message message, TimeSpan timeout = default, CancellationToken ct = default)
        => RequestToRouterAsync(peerRid, new[] { message }, timeout, ct);

    public Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
        => RequestToRouterAsyncInternal(peerRid, parts, timeout, ct)
            .ContinueWith(task => task.Result.Parts, TaskScheduler.Default);

    public void RequestToRouter(RoutingId peerRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan timeout = default)
        => RequestToRouter(peerRid, new[] { message }, callback, flags, timeout);

    public void RequestToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan timeout = default)
        => RequestReplySupport.AttachResultCallback(
            () => RequestToRouterAsyncInternal(peerRid, parts, timeout,
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

    public void ReplyToRouter(RoutingId peerRid, ulong requestSequence,
        Message message, SendFlags flags = SendFlags.None)
        => ReplyToRouter(peerRid, requestSequence, new[] { message }, flags);

    public unsafe void ReplyToRouter(RoutingId peerRid, ulong requestSequence,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        byte[] peerRidBytes = RoutingIdCodec.FromRoutingId(peerRid);
        ZlinkRoutingId routingId = NativeHelpers.WriteRoutingId(peerRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        RequestReplySupport.MovePartsToNative(cloned, out ZlinkMsg[] nativeParts);
        try
        {
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_spot_reply_router(_handle,
                    ref routingId, requestSequence, (IntPtr)nativePtr,
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

    public unsafe Received RecvRouted(RecvFlags flags = RecvFlags.None)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        try
        {
            int rc = NativeMethods.zlink_spot_recv(_handle,
                out IntPtr sourceNodeRid, out IntPtr sourceSpotRid,
                out ulong requestSequence, out nativeParts, out partCount,
                (int)flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());

            Message[] parts = Message.FromNativeVector(nativeParts, partCount);
            nativeParts = IntPtr.Zero;
            partCount = 0;
            RoutingId? nodeRid = sourceNodeRid == IntPtr.Zero ? null :
                RoutingIdCodec.ToRoutingId(
                    NativeHelpers.ReadRoutingId(ref *(ZlinkRoutingId*)sourceNodeRid));
            RoutingId? spotRid = sourceSpotRid == IntPtr.Zero ? null :
                RoutingIdCodec.ToRoutingId(
                    NativeHelpers.ReadRoutingId(ref *(ZlinkRoutingId*)sourceSpotRid));
            if (requestSequence == 0)
                return new Received(nodeRid, parts, spotRid: spotRid);
            return new Received(nodeRid, parts, requestSequence, spotRid,
                (replyParts, sendFlags) => ReplyToSpot(
                    nodeRid ?? throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                    spotRid ?? throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                    requestSequence, replyParts, sendFlags));
        }
        finally
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
        }
    }

    public unsafe void OnRoutedReceive(Action<Received> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        _routedReceiveHandler = handler;
        _routedReceiveHandlerContext = SynchronizationContext.Current;
        _routedReceiveHandlerNative = OnNativeRoutedReceive;
        int rc = NativeMethods.zlink_spot_handler(_handle,
            _routedReceiveHandlerNative, IntPtr.Zero);
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
    }

    public void OnDispatchEvent(Action<SpotDispatchEvent> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        _dispatchEventHandler = handler;
        _dispatchEventHandlerContext = SynchronizationContext.Current;
        _dispatchEventHandlerNative = OnNativeDispatchEvent;
        int rc = NativeMethods.zlink_spot_dispatch_event_handler(_handle,
            _dispatchEventHandlerNative, IntPtr.Zero);
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero && _ownsHandle)
        {
            NativeMethods.zlink_spot_destroy(ref _handle);
        }

        _handle = IntPtr.Zero;

        _sendReadyHandler = null;
        _routedReceiveHandler = null;
        _dispatchEventHandler = null;
        _sendReadyHandlerContext = null;
        _routedReceiveHandlerContext = null;
        _dispatchEventHandlerContext = null;
        _sendReadyHandlerNative = null;
        _routedReceiveHandlerNative = null;
        _dispatchEventHandlerNative = null;
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Spot()
    {
        Dispose();
    }

    private unsafe void PublishCore(string serviceName, string topic,
        ReadOnlySpan<Message> parts, int flags, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackPublishPartLimit
            ? stackalloc ZlinkMsg[StackPublishPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                {
                    throw new ArgumentException(
                        "Parts must not contain null messages.", paramName);
                }
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_spot_publish(_handle, serviceName,
                    topic,
                    (IntPtr)nativePtr, (nuint)parts.Length, (int)flags);
                if (rc < 0)
                {
                    for (int i = 0; i < built; i++)
                        parts[i].RestoreFrom(ref nativeParts[i]);
                    built = 0;
                }
                ZlinkException.ThrowIfError(rc);
            }
        }
        catch
        {
            for (int i = 0; i < built; i++)
                parts[i].RestoreFrom(ref nativeParts[i]);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe void PublishSingleCore(string serviceName, string topic,
        Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_spot_publish(_handle, serviceName,
                topic,
                (IntPtr)(&nativePart), 1, (int)flags);
            if (rc < 0)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (moved)
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
            Runtime.ReportUnhandledCallbackException(ex);
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
                int rc = NativeMethods.zlink_spot_request_spot(_handle,
                    ref nodeRid, ref spotRid, (IntPtr)nativePtr,
                    (nuint)nativeParts.Length, RoutedReplyHandler,
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

    private unsafe Task<Received> RequestServiceAsyncInternal(string serviceName,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
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
                int rc = NativeMethods.zlink_spot_request_service(_handle,
                    serviceName, (IntPtr)nativePtr, (nuint)nativeParts.Length,
                    RoutedReplyHandler, GCHandle.ToIntPtr(handle), flags,
                    timeoutMs);
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

    private unsafe Task<Received> RequestToRouterAsyncInternal(
        RoutingId peerRid, IReadOnlyList<Message> parts, TimeSpan timeout,
        CancellationToken ct, int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        byte[] peerRidBytes = RoutingIdCodec.FromRoutingId(peerRid);
        ZlinkRoutingId routingId = NativeHelpers.WriteRoutingId(peerRidBytes);
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
                int rc = NativeMethods.zlink_spot_request_router(_handle,
                    ref routingId, (IntPtr)nativePtr, (nuint)nativeParts.Length,
                    RoutedReplyHandler, GCHandle.ToIntPtr(handle), flags,
                    timeoutMs);
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

    private unsafe void SendServiceCore(string serviceName, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackPublishPartLimit
            ? stackalloc ZlinkMsg[StackPublishPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                {
                    throw new ArgumentException(
                        "Parts must not contain null messages.", paramName);
                }
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_spot_send_service(_handle,
                    serviceName, (IntPtr)nativePtr, (nuint)parts.Length, flags);
                if (rc < 0)
                {
                    for (int i = 0; i < built; i++)
                        parts[i].RestoreFrom(ref nativeParts[i]);
                    built = 0;
                }
                ZlinkException.ThrowIfError(rc);
            }
        }
        catch
        {
            for (int i = 0; i < built; i++)
                parts[i].RestoreFrom(ref nativeParts[i]);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe void OnNativeRoutedReceive(ZlinkRoutingId* sourceRoutingId,
        ZlinkRoutingId* spotRoutingId, ulong requestSequence, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        Action<Received>? handler = _routedReceiveHandler;
        if (handler == null)
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            return;
        }

        Message[] managedParts = Message.FromNativeVector(parts, partCount);
        parts = IntPtr.Zero;
        partCount = 0;
        RoutingId? nodeRid = sourceRoutingId == null ? null :
            RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref *sourceRoutingId));
        RoutingId? spotRid = spotRoutingId == null ? null :
            RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref *spotRoutingId));
        Received received = requestSequence == 0
            ? new Received(nodeRid, managedParts, spotRid: spotRid)
            : new Received(nodeRid, managedParts, requestSequence, spotRid,
                (replyParts, sendFlags) => ReplyToSpot(
                    nodeRid ?? throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                    spotRid ?? throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                    requestSequence, replyParts, sendFlags));
        CallbackDelivery.Post(_routedReceiveHandlerContext, () => handler(received));
    }

    private void OnNativeDispatchEvent(IntPtr spot, int @event, IntPtr userData)
    {
        Action<SpotDispatchEvent>? handler = _dispatchEventHandler;
        if (handler == null)
            return;
        try
        {
            handler((SpotDispatchEvent)@event);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
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

    private static void EnsureParts(IReadOnlyList<Message> parts, string paramName)
    {
        if (parts == null)
            throw new ArgumentNullException(paramName);
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", paramName);
    }

    private static void ValidateTopicId(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);

        int byteCount = Encoding.UTF8.GetByteCount(value);
        if (byteCount == 0 || byteCount > 255)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "UTF-8 length must be between 1 and 255 bytes.");
        }
    }

    private static void ValidateServiceName(string value, string paramName)
    {
        BoundaryValidation.ValidateFixedUtf8(value, paramName);
    }

    private void PublishPartsWithFlags(string serviceName, string topic,
        IReadOnlyList<Message> parts, int flags, string paramName)
    {
        if (parts is Message[] array)
        {
            PublishCore(serviceName, topic, array.AsSpan(), flags, paramName);
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(serviceName, topic, CollectionsMarshal.AsSpan(list),
                flags, paramName);
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(serviceName, topic, copied.AsSpan(), flags, paramName);
    }

    private SendResult TryPublishPartsWithFlags(string serviceName, string topic,
        IReadOnlyList<Message> parts, string paramName)
    {
        if (parts is Message[] array)
            return TryPublishCore(serviceName, topic, array.AsSpan(), paramName);

        if (parts is List<Message> list)
            return TryPublishCore(serviceName, topic,
                CollectionsMarshal.AsSpan(list), paramName);

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return TryPublishCore(serviceName, topic, copied.AsSpan(), paramName);
    }

    private unsafe TopicMessage SubscribeCore(int flags)
    {
        byte[] serviceBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        try
        {
            nuint serviceLength = TopicBufferSize;
            nuint topicLength = TopicBufferSize;
            ZlinkRoutingId nativeRoutingId = default;
            int rc = NativeMethods.zlink_spot_subscribe(_handle,
                (IntPtr)(&nativeRoutingId),
                out nativeParts, out partCount, serviceBuffer,
                ref serviceLength, topicBuffer, ref topicLength, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());

            RoutingId? routingId = RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref nativeRoutingId));
            string serviceName = DecodeBuffer(serviceBuffer, serviceLength);
            string topic = DecodeBuffer(topicBuffer, topicLength);
            Message[] parts = Message.FromNativeVector(nativeParts, partCount);
            if (parts.Length == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            nativeParts = IntPtr.Zero;
            partCount = 0;
            return new TopicMessage(routingId, serviceName, topic, parts);
        }
        finally
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            ArrayPool<byte>.Shared.Return(serviceBuffer);
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe SubscriptionEvent ReceiveSubscriptionEventCore(int flags)
    {
        byte[] serviceBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint serviceLength = TopicBufferSize;
            nuint topicLength = TopicBufferSize;
            ZlinkRoutingId nativeRoutingId = default;
            int rc = NativeMethods.zlink_spot_subscription_event(_handle,
                (IntPtr)(&nativeRoutingId), out int subscribedInt, serviceBuffer,
                ref serviceLength, topicBuffer, ref topicLength, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());

            RoutingId? routingId = RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref nativeRoutingId));
            string serviceName = DecodeBuffer(serviceBuffer, serviceLength);
            string topic = DecodeBuffer(topicBuffer, topicLength);
            return new SubscriptionEvent(routingId, serviceName, topic,
                subscribedInt != 0);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(serviceBuffer);
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe SendResult TryPublishCore(string serviceName, string topic,
        ReadOnlySpan<Message> parts, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackPublishPartLimit
            ? stackalloc ZlinkMsg[StackPublishPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                {
                    throw new ArgumentException(
                        "Parts must not contain null messages.", paramName);
                }
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                int rc = NativeMethods.zlink_spot_publish(_handle, serviceName,
                    topic,
                    (IntPtr)nativePtr, (nuint)parts.Length, DontWaitFlag);
                if (rc == 0)
                    return SendResult.Sent;

                SendResult? sendResult = TryMapSendResultFromErrno();
                if (sendResult == null)
                {
                    for (int i = 0; i < built; i++)
                        parts[i].RestoreFrom(ref nativeParts[i]);
                    built = 0;
                    throw ZlinkException.FromLastError();
                }
                return sendResult.Value;
            }
        }
        catch
        {
            for (int i = 0; i < built; i++)
                parts[i].RestoreFrom(ref nativeParts[i]);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe SendResult TryPublishSingleCore(string serviceName,
        string topic, Message message)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_spot_publish(_handle, serviceName,
                topic,
                (IntPtr)(&nativePart), 1, DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
            {
                message.RestoreFrom(ref nativePart);
                moved = false;
                throw ZlinkException.FromLastError();
            }
            return sendResult.Value;
        }
        catch
        {
            if (moved)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void PublishRawSingleCore(string serviceName, string topic,
        ReadOnlySpan<byte> payload, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_size(ref nativePart,
                (nuint)payload.Length);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            if (payload.Length != 0)
            {
                IntPtr dataPtr = NativeMethods.zlink_msg_data(ref nativePart);
                if (dataPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");
                payload.CopyTo(new Span<byte>((void*)dataPtr, payload.Length));
            }

            int rc = NativeMethods.zlink_spot_publish(_handle, serviceName,
                topic,
                (IntPtr)(&nativePart), 1, flags);
            if (rc < 0)
            {
                NativeMethods.zlink_msg_close(ref nativePart);
                initialized = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult TryPublishRawSingleCore(string serviceName,
        string topic,
        ReadOnlySpan<byte> payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_size(ref nativePart,
                (nuint)payload.Length);
            ZlinkException.ThrowIfError(initRc);
            initialized = true;

            if (payload.Length != 0)
            {
                IntPtr dataPtr = NativeMethods.zlink_msg_data(ref nativePart);
                if (dataPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");
                payload.CopyTo(new Span<byte>((void*)dataPtr, payload.Length));
            }

            int rc = NativeMethods.zlink_spot_publish(_handle, serviceName,
                topic,
                (IntPtr)(&nativePart), 1, DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            NativeMethods.zlink_msg_close(ref nativePart);
            initialized = false;
            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.FromLastError();
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe void PublishBorrowedSingleCore(string serviceName,
        string topic, byte[] payload, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_spot_publish(_handle, serviceName,
                topic,
                (IntPtr)(&nativePart), 1, flags);
            if (rc < 0)
            {
                NativeMethods.zlink_msg_close(ref nativePart);
                initialized = false;
            }
            ZlinkException.ThrowIfError(rc);
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe SendResult TryPublishBorrowedSingleCore(string serviceName,
        string topic, byte[] payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_spot_publish(_handle, serviceName,
                topic,
                (IntPtr)(&nativePart), 1, DontWaitFlag);
            if (rc == 0)
                return SendResult.Sent;

            NativeMethods.zlink_msg_close(ref nativePart);
            initialized = false;
            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.FromLastError();
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe int ReceiveRawSubscribedFrameCore(Span<byte> destination,
        int flags, out byte[][] pendingFrames)
    {
        IntPtr nativeParts = IntPtr.Zero;
        nuint partCount = 0;
        byte[] serviceBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint serviceLength = TopicBufferSize;
            nuint topicLength = TopicBufferSize;
            int rc = NativeMethods.zlink_spot_subscribe(_handle, IntPtr.Zero,
                out nativeParts, out partCount, serviceBuffer,
                ref serviceLength, topicBuffer, ref topicLength, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            _ = DecodeBuffer(serviceBuffer, serviceLength);
            return CopyFirstFrameAndCollectPending(nativeParts, partCount,
                destination, out pendingFrames);
        }
        finally
        {
            if (nativeParts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(nativeParts, partCount);
            ArrayPool<byte>.Shared.Return(serviceBuffer);
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private static string DecodeBuffer(byte[] buffer, nuint length)
    {
        int boundedLength = length > (nuint)buffer.Length
            ? buffer.Length
            : (int)length;
        return boundedLength == 0
            ? string.Empty
            : Encoding.UTF8.GetString(buffer, 0, boundedLength);
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

    private static void OnBorrowedBufferFree(IntPtr data, IntPtr hint)
    {
        if (hint == IntPtr.Zero)
            return;

        GCHandle.FromIntPtr(hint).Free();
    }
}

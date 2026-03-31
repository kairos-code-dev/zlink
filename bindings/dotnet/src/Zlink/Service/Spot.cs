// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class SpotNode : IDisposable
{
    private IntPtr _handle;

    public SpotNode(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_spot_node_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void Bind(string endpoint)
    {
        ValidateNotEmpty(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_bind(_handle, endpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectPeer(string peerEndpoint)
    {
        ValidateNotEmpty(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_peer(_handle,
            peerEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void DisconnectPeer(string peerEndpoint)
    {
        ValidateNotEmpty(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer(_handle,
            peerEndpoint);
        ZlinkException.ThrowIfError(rc);
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

    public void SetOption(SpotNodeSocketRole role, SocketOptionKey<int> option,
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
        ValidateNotEmpty(certPath, nameof(certPath));
        ValidateNotEmpty(keyPath, nameof(keyPath));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_server(_handle, certPath, keyPath,
            requireClientCert ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        ValidateNotEmpty(caCertPath, nameof(caCertPath));
        ValidateNotEmpty(hostname, nameof(hostname));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_client(_handle, caCertPath,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public SpotNodeStatus Snapshot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_status_snapshot(_handle,
            out var native);
        ZlinkException.ThrowIfError(rc);
        return SpotNodeStatus.FromNative(ref native);
    }

    public SpotNodePeerEntry[] Peers(string? peerEndpoint = null, int? source = null,
        int? state = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodePeerFilter filter = default;
            IntPtr filterPtr = IntPtr.Zero;
            if (!string.IsNullOrEmpty(peerEndpoint) || source.HasValue
                || state.HasValue)
            {
                filter.Source = source ?? 0;
                filter.State = state ?? 0;
                if (!string.IsNullOrEmpty(peerEndpoint))
                {
                    WriteFixedString(peerEndpoint, filter.PeerEndpoint, 256);
                }
                filterPtr = (IntPtr)(&filter);
            }

            return ReadPeerEntries(filterPtr);
        }
    }

    public SpotNodeSubjectEntry[] Subjects(int? role = null, string? subject = null,
        uint? subjectKind = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodeSubjectFilter filter = default;
            IntPtr filterPtr = IntPtr.Zero;
            if (role.HasValue || !string.IsNullOrEmpty(subject)
                || subjectKind.HasValue)
            {
                filter.Role = role ?? 0;
                filter.SubjectKind = subjectKind ?? 0;
                if (!string.IsNullOrEmpty(subject))
                {
                    WriteFixedString(subject, filter.Subject, 256);
                }
                filterPtr = (IntPtr)(&filter);
            }

            return ReadSubjectEntries(filterPtr);
        }
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_spot_node_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
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

    private static void ValidateNotEmpty(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);
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

public sealed class Spot : IDisposable
{
    private const uint DefaultMonitorEvents =
        (1u << 0) | (1u << 4) | (1u << 13) | (1u << 14) | (1u << 17)
        | (1u << 19);
    private const int StackPublishPartLimit = 8;
    private const int TopicBufferSize = 256;
    private const int TopicCacheLimit = 1024;
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
    private SpotSubHandler? _subscribeHandler;
    private Action? _sendReadyHandler;
    private NativeMethods.ZlinkSubscribeHandlerDelegate? _subscribeHandlerNative;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    private readonly ConcurrentDictionary<string, byte[]> _topicCache =
        new(StringComparer.Ordinal);

    internal IntPtr Handle => _handle;

    public Spot(SpotNode node)
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

    public void Publish(string topic, Message message)
    {
        ValidateTopicId(topic, nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        PublishSingleCore(topic, message, 0);
    }

    public SendResult TryPublish(string topic, Message message)
    {
        ValidateTopicId(topic, nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        return TryPublishSingleCore(topic, message);
    }

    public void Publish(string topic, IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        EnsureNotDisposed();
        ValidateTopicId(topic, nameof(topic));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if (parts is Message[] array)
        {
            PublishCore(topic, array.AsSpan(), 0, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(topic, CollectionsMarshal.AsSpan(list), 0,
                nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(topic, copied.AsSpan(), 0, nameof(parts));
    }

    public SendResult TryPublish(string topic, IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        EnsureNotDisposed();
        ValidateTopicId(topic, nameof(topic));
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        return TryPublishPartsWithFlags(topic, parts);
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

    public Subscribed Subscribe()
    {
        EnsureNotDisposed();
        return SubscribeCore(0);
    }

    public Subscribed? TrySubscribe()
    {
        EnsureNotDisposed();
        return TryReceiveCore(() => SubscribeCore(1));
    }

    public unsafe void SubscribeHandler(SpotSubHandler handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        var native = new NativeMethods.ZlinkSubscribeHandlerDelegate(
            OnNativeSubscribe);
        int rc = NativeMethods.zlink_subscribe_handler(_handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
        _subscribeHandler = handler;
        _subscribeHandlerNative = native;
    }

    public void SendReadyHandler(Action handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        int rc = NativeMethods.zlink_send_ready_handler(_handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
        _sendReadyHandler = handler;
        _sendReadyHandlerNative = native;
    }

    public ServiceMonitor OpenMonitor(uint events = DefaultMonitorEvents)
    {
        EnsureNotDisposed();
        var options = new ZlinkServiceMonitorOpenOptions
        {
            Events = events
        };
        IntPtr monitor = NativeMethods.zlink_service_monitor_open(_handle,
            in options);
        if (monitor == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return new ServiceMonitor(monitor);
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero && _ownsHandle)
        {
            NativeMethods.zlink_spot_destroy(ref _handle);
        }

        _handle = IntPtr.Zero;

        _subscribeHandler = null;
        _sendReadyHandler = null;
        _subscribeHandlerNative = null;
        _sendReadyHandlerNative = null;
        GC.SuppressFinalize(this);
    }

    ~Spot()
    {
        Dispose();
    }

    private unsafe void PublishCore(string topic, ReadOnlySpan<Message> parts,
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
                int rc = NativeMethods.zlink_publish(_handle, topic,
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

    private unsafe void PublishSingleCore(string topic, Message message,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_publish(_handle, topic,
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

    private unsafe void OnNativeSubscribe(IntPtr sourceRoutingId, byte* topic,
        nuint topicLen, IntPtr parts, nuint partCount, IntPtr userData)
    {
        SpotSubHandler? handler = _subscribeHandler;
        if (handler == null)
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            return;
        }

        Message[]? managedParts = null;
        bool delivered = false;
        try
        {
            string topicId = topic == null || topicLen == 0
                ? string.Empty
                : Encoding.UTF8.GetString(
                    new ReadOnlySpan<byte>(topic, checked((int)topicLen)));
            managedParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            delivered = true;
            handler(topicId, managedParts);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
            if (!delivered && managedParts != null)
            {
                foreach (Message? part in managedParts)
                    part?.Dispose();
            }
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
        }
    }

    private void OnNativeSendReady(IntPtr subject, IntPtr userData)
    {
        Action? handler = _sendReadyHandler;
        if (handler == null)
            return;

        try
        {
            handler();
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private byte[] GetTopicUtf8(string topicId)
    {
        if (_topicCache.TryGetValue(topicId, out byte[]? cached))
            return cached;

        byte[] encoded = EncodeTopicUtf8(topicId);
        if (_topicCache.Count >= TopicCacheLimit)
            return encoded;

        return _topicCache.GetOrAdd(topicId, encoded);
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

    private static byte[] EncodeTopicUtf8(string topicId)
    {
        int byteCount = Encoding.UTF8.GetByteCount(topicId);
        byte[] bytes = new byte[byteCount + 1];
        Encoding.UTF8.GetBytes(topicId, bytes.AsSpan(0, byteCount));
        bytes[byteCount] = 0;
        return bytes;
    }

    private void PublishPartsWithFlags(string topic, IReadOnlyList<Message> parts,
        int flags)
    {
        if (parts is Message[] array)
        {
            PublishCore(topic, array.AsSpan(), flags, nameof(parts));
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(topic, CollectionsMarshal.AsSpan(list), flags,
                nameof(parts));
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(topic, copied.AsSpan(), flags, nameof(parts));
    }

    private SendResult TryPublishPartsWithFlags(string topic,
        IReadOnlyList<Message> parts)
    {
        if (parts is Message[] array)
            return TryPublishCore(topic, array.AsSpan(), nameof(parts));

        if (parts is List<Message> list)
            return TryPublishCore(topic, CollectionsMarshal.AsSpan(list),
                nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return TryPublishCore(topic, copied.AsSpan(), nameof(parts));
    }

    private unsafe Subscribed SubscribeCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            nuint topicLength = TopicBufferSize;
            int rc = NativeMethods.zlink_subscribe(_handle, IntPtr.Zero,
                out IntPtr nativeParts, out nuint partCount, topicBuffer,
                ref topicLength, flags);
            ZlinkException.ThrowIfError(rc);

            int boundedLength = topicLength > TopicBufferSize - 1
                ? TopicBufferSize - 1
                : (int)topicLength;
            string topic = boundedLength == 0
                ? string.Empty
                : Encoding.UTF8.GetString(topicBuffer, 0, boundedLength);
            Message[] parts = Message.FromNativeVector(nativeParts, partCount);
            return new Subscribed(string.Empty, topic, parts);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe SendResult TryPublishCore(string topic,
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
                int rc = NativeMethods.zlink_publish(_handle, topic,
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

    private unsafe SendResult TryPublishSingleCore(string topic, Message message)
    {
        ZlinkMsg nativePart = default;
        bool moved = false;
        try
        {
            message.MoveTo(ref nativePart);
            moved = true;
            int rc = NativeMethods.zlink_publish(_handle, topic,
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

    private static T? TryReceiveCore<T>(Func<T> operation) where T : class
    {
        try
        {
            return operation();
        }
        catch (ZlinkException ex) when (ZlinkException.MapErrorCode(ex.Errno)
            == ErrorCode.EAgain)
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
}

/// <summary>
/// Managed subscribe callback.
/// Message payload is copied to managed <see cref="Message"/> instances.
/// The callback owns those managed messages and must dispose each part exactly once.
/// </summary>
public delegate void SpotSubHandler(string topicId, Message[] parts);

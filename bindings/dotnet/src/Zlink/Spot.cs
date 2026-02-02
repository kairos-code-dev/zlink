using System;
using Zlink.Native;

namespace Zlink;

public sealed class SpotNode : IDisposable
{
    private IntPtr _handle;

    public SpotNode(Context context)
    {
        _handle = NativeMethods.zlink_spot_node_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void Bind(string endpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_bind(_handle, endpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectRegistry(string registryEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_registry(_handle,
            registryEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectPeerPub(string peerPubEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_peer_pub(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void DisconnectPeerPub(string peerPubEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer_pub(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Register(string serviceName, string advertiseEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_register(_handle, serviceName,
            advertiseEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unregister(string serviceName)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_unregister(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetDiscovery(Discovery discovery, string serviceName)
    {
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        int rc = NativeMethods.zlink_spot_node_set_discovery(_handle,
            discovery.Handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsServer(string cert, string key)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_tls_server(_handle, cert,
            key);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsClient(string caCert, string hostname, bool trustSystem)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_tls_client(_handle, caCert,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public Socket CreatePubSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_spot_node_pub_socket(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public Socket CreateSubSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_spot_node_sub_socket(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
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
}

public sealed class Spot : IDisposable
{
    private IntPtr _handle;

    public Spot(SpotNode node)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        _handle = NativeMethods.zlink_spot_new(node.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public void TopicCreate(string topicId, SpotTopicMode mode)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_topic_create(_handle, topicId,
            (int)mode);
        ZlinkException.ThrowIfError(rc);
    }

    public void TopicDestroy(string topicId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_topic_destroy(_handle, topicId);
        ZlinkException.ThrowIfError(rc);
    }

    public void Publish(string topicId, Message[] parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Length == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        var tmp = new ZlinkMsg[parts.Length];
        int built = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].CopyTo(ref tmp[i]);
                built++;
            }
            int rc = NativeMethods.zlink_spot_publish(_handle, topicId, tmp,
                (nuint)tmp.Length, (int)flags);
            ZlinkException.ThrowIfError(rc);
        }
        finally
        {
            for (int i = 0; i < built; i++)
            {
                NativeMethods.zlink_msg_close(ref tmp[i]);
            }
        }
    }

    public void Subscribe(string topicId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_subscribe(_handle, topicId);
        ZlinkException.ThrowIfError(rc);
    }

    public void SubscribePattern(string pattern)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_subscribe_pattern(_handle, pattern);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unsubscribe(string topicIdOrPattern)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_unsubscribe(_handle,
            topicIdOrPattern);
        ZlinkException.ThrowIfError(rc);
    }

    public SpotMessage Receive(ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        unsafe
        {
            byte* topicBuf = stackalloc byte[256];
            nuint topicLen = 256;
            int rc = NativeMethods.zlink_spot_recv(_handle, out var parts,
                out var count, (int)flags, topicBuf, ref topicLen);
            if (rc != 0)
                throw ZlinkException.FromLastError();
            string topic = NativeHelpers.ReadString(topicBuf, (int)topicLen);
            Message[] messages = Message.FromNativeVector(parts, count);
            return new SpotMessage(topic, messages);
        }
    }

    public Socket CreatePubSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_spot_pub_socket(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public Socket CreateSubSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_spot_sub_socket(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_spot_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Spot()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Spot));
    }
}

public readonly struct SpotMessage
{
    public SpotMessage(string topicId, Message[] parts)
    {
        TopicId = topicId;
        Parts = parts;
    }

    public string TopicId { get; }
    public Message[] Parts { get; }
}

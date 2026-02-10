// SPDX-License-Identifier: MPL-2.0

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
    private readonly IntPtr _nodeHandle;
    private IntPtr _pubHandle;
    private IntPtr _subHandle;

    public Spot(SpotNode node)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        _nodeHandle = node.Handle;
        _pubHandle = NativeMethods.zlink_spot_pub_new(_nodeHandle);
        _subHandle = NativeMethods.zlink_spot_sub_new(_nodeHandle);
        if (_pubHandle == IntPtr.Zero || _subHandle == IntPtr.Zero)
        {
            if (_pubHandle != IntPtr.Zero)
                NativeMethods.zlink_spot_pub_destroy(ref _pubHandle);
            if (_subHandle != IntPtr.Zero)
                NativeMethods.zlink_spot_sub_destroy(ref _subHandle);
            throw ZlinkException.FromLastError();
        }
    }

    public void TopicCreate(string topicId, SpotTopicMode mode)
    {
        EnsureNotDisposed();
        if (string.IsNullOrEmpty(topicId))
            throw new ArgumentException("Topic ID must not be empty.", nameof(topicId));
        _ = mode;
    }

    public void TopicDestroy(string topicId)
    {
        EnsureNotDisposed();
        if (string.IsNullOrEmpty(topicId))
            throw new ArgumentException("Topic ID must not be empty.", nameof(topicId));
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
        for (int i = 0; i < parts.Length; i++)
        {
            parts[i].CopyTo(ref tmp[i]);
            built++;
        }
        int rc = NativeMethods.zlink_spot_pub_publish(_pubHandle, topicId, tmp,
            (nuint)tmp.Length, (int)flags);
        if (rc < 0)
        {
            for (int i = 0; i < built; i++)
            {
                NativeMethods.zlink_msg_close(ref tmp[i]);
            }
        }
        ZlinkException.ThrowIfError(rc);
    }

    public void Subscribe(string topicId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_subscribe(_subHandle, topicId);
        ZlinkException.ThrowIfError(rc);
    }

    public void SubscribePattern(string pattern)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_subscribe_pattern(_subHandle, pattern);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unsubscribe(string topicIdOrPattern)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_sub_unsubscribe(_subHandle,
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
            int rc = NativeMethods.zlink_spot_sub_recv(_subHandle, out var parts,
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
        IntPtr handle = NativeMethods.zlink_spot_node_pub_socket(_nodeHandle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public Socket CreateSubSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_spot_sub_socket(_subHandle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public void Dispose()
    {
        if (_pubHandle != IntPtr.Zero)
        {
            NativeMethods.zlink_spot_pub_destroy(ref _pubHandle);
            _pubHandle = IntPtr.Zero;
        }
        if (_subHandle != IntPtr.Zero)
        {
            NativeMethods.zlink_spot_sub_destroy(ref _subHandle);
            _subHandle = IntPtr.Zero;
        }
        GC.SuppressFinalize(this);
    }

    ~Spot()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_pubHandle == IntPtr.Zero || _subHandle == IntPtr.Zero)
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

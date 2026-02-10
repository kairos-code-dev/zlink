// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink;

public sealed class Socket : IDisposable
{
    private IntPtr _handle;
    private readonly bool _own;

    public Socket(Context context, SocketType type)
    {
        _handle = NativeMethods.zlink_socket(context.Handle, (int)type);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        _own = true;
    }

    internal Socket(IntPtr handle, bool own)
    {
        _handle = handle;
        _own = own;
    }

    internal static Socket Adopt(IntPtr handle, bool own)
    {
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Invalid socket handle.", nameof(handle));
        return new Socket(handle, own);
    }

    public IntPtr Handle => _handle;

    public void Bind(string address)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_bind(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Connect(string address)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_connect(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unbind(string address)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_unbind(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Disconnect(string address)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_disconnect(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public int Send(byte[] buffer, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        int rc = NativeMethods.zlink_send(_handle, buffer, (nuint)buffer.Length,
            (int)flags);
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public int SendConst(byte[] buffer, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        int rc = NativeMethods.zlink_send_const(_handle, buffer,
            (nuint)buffer.Length, (int)flags);
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public int Receive(byte[] buffer, ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        int rc = NativeMethods.zlink_recv(_handle, buffer, (nuint)buffer.Length,
            (int)flags);
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        int rc = NativeMethods.zlink_msg_send(ref message.Handle, _handle,
            (int)flags);
        ZlinkException.ThrowIfError(rc);
        if (rc >= 0)
            message.Dispose();
    }

    public Message ReceiveMessage(ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        var msg = new Message();
        int rc = NativeMethods.zlink_msg_recv(ref msg.Handle, _handle,
            (int)flags);
        ZlinkException.ThrowIfError(rc);
        return msg;
    }

    public void SetOption(SocketOption option, int value)
    {
        EnsureNotDisposed();
        unsafe
        {
            int tmp = value;
            IntPtr ptr = new IntPtr(&tmp);
            int rc = NativeMethods.zlink_setsockopt(_handle, (int)option, ptr,
                (nuint)sizeof(int));
            ZlinkException.ThrowIfError(rc);
        }
    }

    public void SetOption(SocketOption option, byte[] value)
    {
        EnsureNotDisposed();
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        unsafe
        {
            fixed (byte* ptr = value)
            {
                int rc = NativeMethods.zlink_setsockopt(_handle, (int)option,
                    (IntPtr)ptr, (nuint)value.Length);
                ZlinkException.ThrowIfError(rc);
            }
        }
    }

    public void SetOption(SocketOption option, string value)
    {
        EnsureNotDisposed();
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        byte[] buffer = System.Text.Encoding.UTF8.GetBytes(value);
        SetOption(option, buffer);
    }

    public int GetOption(SocketOption option)
    {
        EnsureNotDisposed();
        unsafe
        {
            int value = 0;
            nuint size = (nuint)sizeof(int);
            IntPtr ptr = new IntPtr(&value);
            int rc = NativeMethods.zlink_getsockopt(_handle, (int)option, ptr,
                ref size);
            ZlinkException.ThrowIfError(rc);
            return value;
        }
    }

    public byte[] GetOptionBytes(SocketOption option, int initialSize = 256)
    {
        EnsureNotDisposed();
        if (initialSize <= 0)
            throw new ArgumentOutOfRangeException(nameof(initialSize));
        byte[] buffer = new byte[initialSize];
        unsafe
        {
            fixed (byte* ptr = buffer)
            {
                nuint size = (nuint)buffer.Length;
                int rc = NativeMethods.zlink_getsockopt(_handle, (int)option,
                    (IntPtr)ptr, ref size);
                if (rc != 0 && size > (nuint)buffer.Length)
                {
                    buffer = new byte[(int)size];
                    fixed (byte* ptr2 = buffer)
                    {
                        rc = NativeMethods.zlink_getsockopt(_handle, (int)option,
                            (IntPtr)ptr2, ref size);
                    }
                }
                ZlinkException.ThrowIfError(rc);
                if (size == (nuint)buffer.Length)
                    return buffer;
                byte[] resized = new byte[(int)size];
                Array.Copy(buffer, resized, resized.Length);
                return resized;
            }
        }
    }

    public string GetOptionString(SocketOption option, int initialSize = 256)
    {
        byte[] bytes = GetOptionBytes(option, initialSize);
        int len = Array.IndexOf(bytes, (byte)0);
        if (len < 0)
            len = bytes.Length;
        return System.Text.Encoding.UTF8.GetString(bytes, 0, len);
    }

    public void Monitor(string address, SocketEvent events)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_monitor(_handle, address,
            (int)events);
        ZlinkException.ThrowIfError(rc);
    }

    public MonitorSocket MonitorOpen(SocketEvent events)
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_socket_monitor_open(_handle,
            (int)events);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return new MonitorSocket(Socket.Adopt(handle, true));
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        if (_own)
            NativeMethods.zlink_close(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Socket()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Socket));
    }
}

// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink;

public sealed class Message : IDisposable
{
    private ZlinkMsg _msg;
    private bool _valid;

    public Message()
    {
        Init();
    }

    public Message(int size)
    {
        if (size < 0)
            throw new ArgumentOutOfRangeException(nameof(size));
        int rc = NativeMethods.zlink_msg_init_size(ref _msg, (nuint)size);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        _valid = true;
    }

    private Message(bool init)
    {
        if (init)
            Init();
    }

    public int Size
    {
        get
        {
            EnsureValid();
            return (int)NativeMethods.zlink_msg_size(ref _msg);
        }
    }

    public bool More
    {
        get
        {
            EnsureValid();
            return NativeMethods.zlink_msg_more(ref _msg) != 0;
        }
    }

    public byte[] ToArray()
    {
        EnsureValid();
        nuint size = NativeMethods.zlink_msg_size(ref _msg);
        if (size == 0)
            return Array.Empty<byte>();
        IntPtr data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
            return Array.Empty<byte>();
        byte[] buffer = new byte[(int)size];
        Marshal.Copy(data, buffer, 0, buffer.Length);
        return buffer;
    }

    public static Message FromBytes(byte[] data)
    {
        if (data == null)
            throw new ArgumentNullException(nameof(data));
        var msg = new Message(data.Length);
        if (data.Length == 0)
            return msg;
        IntPtr dest = NativeMethods.zlink_msg_data(ref msg._msg);
        Marshal.Copy(data, 0, dest, data.Length);
        return msg;
    }

    public int GetProperty(int property)
    {
        EnsureValid();
        return NativeMethods.zlink_msg_get(ref _msg, property);
    }

    public void SetProperty(int property, int value)
    {
        EnsureValid();
        int rc = NativeMethods.zlink_msg_set(ref _msg, property, value);
        ZlinkException.ThrowIfError(rc);
    }

    public string? GetPropertyString(string property)
    {
        EnsureValid();
        IntPtr ptr = NativeMethods.zlink_msg_gets(ref _msg, property);
        if (ptr == IntPtr.Zero)
            return null;
        return Marshal.PtrToStringAnsi(ptr);
    }

    public void Dispose()
    {
        Close();
        GC.SuppressFinalize(this);
    }

    ~Message()
    {
        Close();
    }

    internal ref ZlinkMsg Handle => ref _msg;

    internal bool IsValid => _valid;

    internal void Init()
    {
        if (_valid)
            return;
        int rc = NativeMethods.zlink_msg_init(ref _msg);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        _valid = true;
    }

    internal void MoveTo(ref ZlinkMsg dest)
    {
        EnsureValid();
        int rc = NativeMethods.zlink_msg_init(ref dest);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        rc = NativeMethods.zlink_msg_move(ref dest, ref _msg);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        _valid = false;
    }

    internal void CopyTo(ref ZlinkMsg dest)
    {
        EnsureValid();
        int rc = NativeMethods.zlink_msg_init(ref dest);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        rc = NativeMethods.zlink_msg_copy(ref dest, ref _msg);
        if (rc != 0)
            throw ZlinkException.FromLastError();
    }

    internal static Message[] FromNativeVector(IntPtr parts, nuint count)
    {
        if (parts == IntPtr.Zero || count == 0)
            return Array.Empty<Message>();
        int length = checked((int)count);
        Message[] result = new Message[length];
        try
        {
            unsafe
            {
                ZlinkMsg* src = (ZlinkMsg*)parts;
                for (int i = 0; i < length; i++)
                {
                    var msg = new Message(false);
                    msg.Init();
                    int rc = NativeMethods.zlink_msg_copy(ref msg._msg,
                        ref src[i]);
                    if (rc != 0)
                        throw ZlinkException.FromLastError();
                    result[i] = msg;
                }
            }
        }
        finally
        {
            NativeMethods.zlink_msgv_close(parts, count);
        }
        return result;
    }

    private void Close()
    {
        if (!_valid)
            return;
        NativeMethods.zlink_msg_close(ref _msg);
        _valid = false;
    }

    private void EnsureValid()
    {
        if (!_valid)
            throw new ObjectDisposedException(nameof(Message));
    }
}

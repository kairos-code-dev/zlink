// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public sealed class Message : IDisposable, IAsyncDisposable
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
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _valid = true;
    }

    public Message(ReadOnlySpan<byte> data) : this(data.Length)
    {
        if (data.Length == 0)
            return;
        unsafe
        {
            IntPtr dest = NativeMethods.zlink_msg_data(ref _msg);
            if (dest == IntPtr.Zero)
                throw new InvalidOperationException("Message data is null.");
            data.CopyTo(new Span<byte>((void*)dest, data.Length));
        }
    }

    public Message(ReadOnlyMemory<byte> data) : this(data.Span)
    {
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

    public int RefCount
    {
        get
        {
            EnsureValid();
            return NativeMethods.zlink_msg_refcnt(ref _msg);
        }
    }

    public byte[] ToArray()
    {
        return AsReadOnlySpan().ToArray();
    }

    public unsafe ReadOnlySpan<byte> AsReadOnlySpan()
    {
        EnsureValid();
        nuint size = NativeMethods.zlink_msg_size(ref _msg);
        if (size == 0)
            return ReadOnlySpan<byte>.Empty;
        IntPtr data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
            return ReadOnlySpan<byte>.Empty;
        return new ReadOnlySpan<byte>((void*)data, (int)size);
    }

    public ReadOnlyMemory<byte> AsReadOnlyMemory()
    {
        return ToArray();
    }

    public int CopyTo(Span<byte> destination)
    {
        if (!TryCopyTo(destination, out int bytesWritten))
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        return bytesWritten;
    }

    public int CopyTo(IBufferWriter<byte> destination)
    {
        if (destination == null)
            throw new ArgumentNullException(nameof(destination));

        int size = Size;
        Span<byte> span = destination.GetSpan(size).Slice(0, size);
        int written = CopyTo(span);
        destination.Advance(written);
        return written;
    }

    public unsafe bool TryCopyTo(Span<byte> destination, out int bytesWritten)
    {
        EnsureValid();
        nuint size = NativeMethods.zlink_msg_size(ref _msg);
        if (size == 0)
        {
            bytesWritten = 0;
            return true;
        }
        if (size > (nuint)destination.Length)
        {
            bytesWritten = 0;
            return false;
        }
        IntPtr data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
        {
            bytesWritten = 0;
            return true;
        }
        new ReadOnlySpan<byte>((void*)data, (int)size).CopyTo(destination);
        bytesWritten = (int)size;
        return true;
    }

    public static Message FromBytes(byte[] data)
    {
        if (data == null)
            throw new ArgumentNullException(nameof(data));
        return FromBytes(data.AsSpan());
    }

    public static Message FromBytes(ReadOnlySpan<byte> data)
    {
        return new Message(data);
    }

    public static Message FromBytes(ReadOnlyMemory<byte> data)
    {
        return new Message(data);
    }

    public static Message FromSequence(ReadOnlySequence<byte> data)
    {
        if (data.IsSingleSegment)
            return new Message(data.First);
        return new Message((ReadOnlySpan<byte>)data.ToArray());
    }

    public static Message FromString(string value)
    {
        return FromString(value, Encoding.UTF8);
    }

    public static Message FromString(string value, Encoding encoding)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        if (encoding == null)
            throw new ArgumentNullException(nameof(encoding));
        return new Message((ReadOnlySpan<byte>)encoding.GetBytes(value));
    }

    public string GetString()
    {
        return GetString(Encoding.UTF8);
    }

    public string GetString(Encoding encoding)
    {
        if (encoding == null)
            throw new ArgumentNullException(nameof(encoding));
        return encoding.GetString(AsReadOnlySpan());
    }

    public string? GetProperty(string property)
    {
        EnsureValid();
        IntPtr ptr = NativeMethods.zlink_msg_gets(ref _msg, property);
        if (ptr == IntPtr.Zero)
            return null;
        return Marshal.PtrToStringUTF8(ptr);
    }

    public void Dispose()
    {
        Close();
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
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
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _valid = true;
    }

    /// <summary>
    /// Moves native ownership to a new <see cref="Message"/> instance.
    /// </summary>
    /// <returns>The destination message that now owns the payload.</returns>
    /// <remarks>
    /// After a successful move, this instance becomes invalid and any further
    /// access throws <see cref="ObjectDisposedException"/>.
    /// </remarks>
    public Message Move()
    {
        var moved = new Message(false);
        MoveTo(ref moved._msg);
        moved._valid = true;
        return moved;
    }

    public Message Copy()
    {
        EnsureValid();
        var copy = new Message(false);
        CopyTo(ref copy._msg);
        copy._valid = true;
        return copy;
    }

    internal void MoveTo(ref ZlinkMsg dest)
    {
        EnsureValid();
            int rc = NativeMethods.zlink_msg_init(ref dest);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            rc = NativeMethods.zlink_msg_move(ref dest, ref _msg);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            _valid = false;
        }
        catch
        {
            try
            {
                NativeMethods.zlink_msg_close(ref dest);
            }
            catch
            {
            }
            throw;
        }
    }

    internal void RestoreFrom(ref ZlinkMsg src)
    {
        if (_valid)
            throw new InvalidOperationException(
                "RestoreFrom requires an invalid message state.");

        int rc = NativeMethods.zlink_msg_init(ref _msg);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            rc = NativeMethods.zlink_msg_move(ref _msg, ref src);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            _valid = true;
        }
        catch
        {
            try
            {
                NativeMethods.zlink_msg_close(ref _msg);
            }
            catch
            {
            }
            throw;
        }
    }

    internal void CopyTo(ref ZlinkMsg dest)
    {
        EnsureValid();
        int rc = NativeMethods.zlink_msg_init(ref dest);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            rc = NativeMethods.zlink_msg_copy(ref dest, ref _msg);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        }
        catch
        {
            try
            {
                NativeMethods.zlink_msg_close(ref dest);
            }
            catch
            {
            }
            throw;
        }
    }

    internal static unsafe int CopySinglePartPayload(IntPtr parts, nuint count,
        Span<byte> destination)
    {
        if (parts == IntPtr.Zero || count == 0)
            return 0;
        if (count != 1)
            throw new InvalidOperationException(
                "Expected a single-part message.");

        ZlinkMsg* msgv = (ZlinkMsg*)parts;
        int payloadSize = (int)NativeMethods.zlink_msg_size(ref msgv[0]);
        if (payloadSize <= 0)
            return 0;

        IntPtr payloadPtr = NativeMethods.zlink_msg_data(ref msgv[0]);
        if (payloadPtr == IntPtr.Zero)
            return 0;

        int toCopy = payloadSize;
        if (toCopy > destination.Length)
            toCopy = destination.Length;
        new ReadOnlySpan<byte>((void*)payloadPtr, toCopy).CopyTo(destination);
        return payloadSize;
    }

    internal static unsafe Message[] CopyFromNativeReadOnlyVector(IntPtr parts,
        nuint count)
    {
        if (parts == IntPtr.Zero || count == 0)
            return Array.Empty<Message>();

        int length = checked((int)count);
        Message[] result = new Message[length];
        int built = 0;
        try
        {
            ZlinkMsg* msgv = (ZlinkMsg*)parts;
            for (int i = 0; i < length; i++)
            {
                int size = checked((int)NativeMethods.zlink_msg_size(ref msgv[i]));
                if (size <= 0)
                {
                    result[i] = new Message(0);
                }
                else
                {
                    IntPtr payloadPtr = NativeMethods.zlink_msg_data(ref msgv[i]);
                    if (payloadPtr == IntPtr.Zero)
                    {
                        result[i] = new Message(0);
                    }
                    else
                    {
                        ReadOnlySpan<byte> payload = new ReadOnlySpan<byte>(
                            (void*)payloadPtr, size);
                        result[i] = new Message(payload);
                    }
                }
                built++;
            }
            return result;
        }
        catch
        {
            for (int i = 0; i < built; i++)
                result[i]?.Dispose();
            throw;
        }
    }

    internal static Message[] FromNativeVector(IntPtr parts, nuint count)
    {
        if (parts == IntPtr.Zero || count == 0)
            return Array.Empty<Message>();
        int length = checked((int)count);
        Message[] result = new Message[length];
        int built = 0;
        try
        {
            unsafe
            {
                ZlinkMsg* src = (ZlinkMsg*)parts;
                for (int i = 0; i < length; i++)
                {
                    var msg = new Message(false);
                    msg.Init();
                    int rc = NativeMethods.zlink_msg_move(ref msg._msg,
                        ref src[i]);
                    if (rc != 0)
                    {
                        msg.Dispose();
                        throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
                    }
                    result[i] = msg;
                    built++;
                }
            }
        }
        catch
        {
            for (int i = 0; i < built; i++)
                result[i]?.Dispose();
            throw;
        }
        finally
        {
            NativeMethods.zlink_multipart_close(parts, count);
        }
        return result;
    }

    internal static unsafe Message MoveFromNativeSingle(IntPtr message)
    {
        if (message == IntPtr.Zero)
            throw new ArgumentNullException(nameof(message));

        var result = new Message(false);
        result.Init();
        try
        {
            ZlinkMsg* src = (ZlinkMsg*)message;
            int rc = NativeMethods.zlink_msg_move(ref result._msg, ref *src);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            return result;
        }
        catch
        {
            result.Dispose();
            throw;
        }
    }

    internal static unsafe Message CopyFromNativeSingle(IntPtr message)
    {
        if (message == IntPtr.Zero)
            throw new ArgumentNullException(nameof(message));

        ZlinkMsg* src = (ZlinkMsg*)message;
        int size = checked((int)NativeMethods.zlink_msg_size(ref *src));
        if (size <= 0)
            return new Message(0);

        IntPtr payloadPtr = NativeMethods.zlink_msg_data(ref *src);
        if (payloadPtr == IntPtr.Zero)
            return new Message(0);

        ReadOnlySpan<byte> payload = new ReadOnlySpan<byte>((void*)payloadPtr, size);
        return new Message(payload);
    }

    internal void DetachAfterSend()
    {
        EnsureValid();
        _valid = false;
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

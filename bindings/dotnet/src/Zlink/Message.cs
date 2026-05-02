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
    private byte[]? _managedBytes;
    private int _managedLength;
    private GCHandle _borrowedPinnedHandle;
    private GCHandle _selfHandle;
    private bool _borrowedInFlight;
    private bool _disposeAfterBorrowedSend;

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

    internal Message(bool init)
    {
        if (init)
            Init();
    }

    public int Size
    {
        get
        {
            EnsureValid();
            if (_managedBytes != null)
                return _managedLength;
            return (int)NativeMethods.zlink_msg_size(ref _msg);
        }
    }

    public int RefCount
    {
        get
        {
            EnsureValid();
            if (_managedBytes != null)
                return 1;
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
        if (_managedBytes != null)
            return _managedBytes.AsSpan(0, _managedLength);
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
        if (_managedBytes != null)
            return _managedBytes.AsMemory(0, _managedLength);
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
        if (_managedBytes != null)
        {
            if (_managedLength > destination.Length)
            {
                bytesWritten = 0;
                return false;
            }

            _managedBytes.AsSpan(0, _managedLength).CopyTo(destination);
            bytesWritten = _managedLength;
            return true;
        }
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
        var message = new Message(false);
        message.InitializeManagedCopy(data.AsSpan());
        return message;
    }

    public static Message FromOwnedBytes(byte[] data)
    {
        if (data == null)
            throw new ArgumentNullException(nameof(data));
        var message = new Message(false);
        message.InitializeManagedOwned(data);
        return message;
    }

    public static Message FromBytes(ReadOnlySpan<byte> data)
    {
        var message = new Message(false);
        message.InitializeManagedCopy(data);
        return message;
    }

    public static Message FromBytes(ReadOnlyMemory<byte> data)
    {
        var message = new Message(false);
        message.InitializeManagedCopy(data.Span);
        return message;
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
        if (_managedBytes != null)
            return null;
        IntPtr ptr = NativeMethods.zlink_msg_gets(ref _msg, property);
        if (ptr == IntPtr.Zero)
            return null;
        return Marshal.PtrToStringUTF8(ptr);
    }

    public void Dispose()
    {
        Close();
        if (_borrowedInFlight)
            _disposeAfterBorrowedSend = true;
        else
            ReleaseSelfHandle();
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
        ReleaseSelfHandle();
    }

    internal ref ZlinkMsg Handle => ref _msg;

    internal bool IsValid => _valid;

    internal void AdoptInitializedNative()
    {
        _valid = true;
    }

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
        if (_managedBytes != null)
        {
            var movedManaged = new Message(false)
            {
                _managedBytes = _managedBytes,
                _managedLength = _managedLength,
                _valid = true
            };
            _managedBytes = null;
            _managedLength = 0;
            _valid = false;
            return movedManaged;
        }

        var moved = new Message(false);
        MoveTo(ref moved._msg);
        moved._valid = true;
        return moved;
    }

    public Message Copy()
    {
        EnsureValid();
        if (_managedBytes != null)
            MaterializeManagedBytes();
        var copy = new Message(false);
        CopyTo(ref copy._msg);
        copy._valid = true;
        return copy;
    }

    internal unsafe void MoveTo(ref ZlinkMsg dest)
    {
        EnsureValid();
        int rc = _managedBytes != null
            ? NativeMethods.zlink_msg_init_size(ref dest, (nuint)_managedLength)
            : NativeMethods.zlink_msg_init(ref dest);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            if (_managedBytes != null)
            {
                if (_managedLength != 0)
                {
                    IntPtr destPtr = NativeMethods.zlink_msg_data(ref dest);
                    if (destPtr == IntPtr.Zero)
                        throw new InvalidOperationException("Message data is null.");
                    _managedBytes.AsSpan(0, _managedLength).CopyTo(
                        new Span<byte>((void*)destPtr, _managedLength));
                }
                ReleaseManagedBytes();
            }
            else
            {
                rc = NativeMethods.zlink_msg_move(ref dest, ref _msg);
                if (rc != 0)
                    throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            }
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

        _managedBytes = null;
        _managedLength = 0;
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

    internal unsafe void CopyTo(ref ZlinkMsg dest)
    {
        EnsureValid();
        int rc = _managedBytes != null
            ? NativeMethods.zlink_msg_init_size(ref dest, (nuint)_managedLength)
            : NativeMethods.zlink_msg_init(ref dest);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            if (_managedBytes != null)
            {
                if (_managedLength != 0)
                {
                    IntPtr destPtr = NativeMethods.zlink_msg_data(ref dest);
                    if (destPtr == IntPtr.Zero)
                        throw new InvalidOperationException("Message data is null.");
                    _managedBytes.AsSpan(0, _managedLength).CopyTo(
                        new Span<byte>((void*)destPtr, _managedLength));
                }
            }
            else
            {
                rc = NativeMethods.zlink_msg_copy(ref dest, ref _msg);
                if (rc != 0)
                    throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            }
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

    internal static Message MoveFromNative(ref ZlinkMsg source)
    {
        var result = new Message(false);
        result.Init();
        try
        {
            int rc = NativeMethods.zlink_msg_move(ref result._msg, ref source);
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
        if (!_borrowedInFlight)
            ReleaseManagedBytes();
        _valid = false;
    }

    internal bool TryPrepareBorrowedSend(out IntPtr data, out int length,
        out IntPtr hint)
    {
        EnsureValid();
        if (_managedBytes == null || _borrowedInFlight)
        {
            data = IntPtr.Zero;
            length = 0;
            hint = IntPtr.Zero;
            return false;
        }

        _borrowedPinnedHandle = GCHandle.Alloc(_managedBytes, GCHandleType.Pinned);
        if (!_selfHandle.IsAllocated)
            _selfHandle = GCHandle.Alloc(this);
        _borrowedInFlight = true;
        data = _managedLength == 0
            ? IntPtr.Zero
            : _borrowedPinnedHandle.AddrOfPinnedObject();
        length = _managedLength;
        hint = GCHandle.ToIntPtr(_selfHandle);
        return true;
    }

    internal void DetachAfterPreparedSend()
    {
        EnsureValid();
        _valid = false;
    }

    internal void CancelBorrowedSendPrepare()
    {
        if (_borrowedPinnedHandle.IsAllocated)
            _borrowedPinnedHandle.Free();
        _borrowedInFlight = false;
        _disposeAfterBorrowedSend = false;
    }

    internal static void CompleteBorrowedSend(GCHandle handle)
    {
        if (handle.Target is Message message)
            message.CompleteBorrowedSendCore();
    }

    private void Close()
    {
        if (!_valid)
            return;
        if (_managedBytes == null)
            NativeMethods.zlink_msg_close(ref _msg);
        if (!_borrowedInFlight)
            ReleaseManagedBytes();
        _valid = false;
    }

    private void EnsureValid()
    {
        if (!_valid)
            throw new ObjectDisposedException(nameof(Message));
    }

    private void InitializeManagedCopy(ReadOnlySpan<byte> data)
    {
        _managedBytes = data.ToArray();
        _managedLength = data.Length;
        _valid = true;
    }

    private void InitializeManagedOwned(byte[] data)
    {
        _managedBytes = data;
        _managedLength = data.Length;
        _valid = true;
    }

    private unsafe void MaterializeManagedBytes()
    {
        if (_managedBytes == null)
            return;

        ZlinkMsg native = default;
        int initRc = NativeMethods.zlink_msg_init_size(ref native,
            (nuint)_managedLength);
        if (initRc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());

        try
        {
            if (_managedLength != 0)
            {
                IntPtr destPtr = NativeMethods.zlink_msg_data(ref native);
                if (destPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");

                _managedBytes.AsSpan(0, _managedLength).CopyTo(
                    new Span<byte>((void*)destPtr, _managedLength));
            }

            _msg = native;
            _managedBytes = null;
            _managedLength = 0;
        }
        catch
        {
            NativeMethods.zlink_msg_close(ref native);
            throw;
        }
    }

    private void ReleaseManagedBytes()
    {
        _managedBytes = null;
        _managedLength = 0;
    }

    private void CompleteBorrowedSendCore()
    {
        if (_borrowedPinnedHandle.IsAllocated)
            _borrowedPinnedHandle.Free();
        _borrowedInFlight = false;
        ReleaseManagedBytes();
        if (_disposeAfterBorrowedSend)
        {
            _disposeAfterBorrowedSend = false;
            ReleaseSelfHandle();
        }
    }

    private void ReleaseSelfHandle()
    {
        if (_selfHandle.IsAllocated)
            _selfHandle.Free();
    }
}

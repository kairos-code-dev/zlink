// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections;
using System.Collections.Generic;
using System.Threading;
using Zlink.Native;

namespace Zlink;

internal sealed class MultipartMessageCollection : IReadOnlyList<Message>, IDisposable
{
    private readonly object _gate = new();
    private readonly Message?[] _messages;
    private ZlinkMsg[]? _nativeParts;
    private int _closed;

    private MultipartMessageCollection(Message[] messages)
    {
        _messages = new Message?[messages.Length];
        for (int i = 0; i < messages.Length; i++)
            _messages[i] = messages[i];
    }

    private MultipartMessageCollection(ZlinkMsg[] nativeParts)
    {
        _messages = new Message?[nativeParts.Length];
        _nativeParts = nativeParts;
    }

    internal static MultipartMessageCollection FromMessages(Message[] messages)
    {
        return new MultipartMessageCollection(messages ?? Array.Empty<Message>());
    }

    internal static MultipartMessageCollection FromSingle(Message message)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return new MultipartMessageCollection(new[] { message });
    }

    internal static MultipartMessageCollection FromNativeParts(ZlinkMsg[] nativeParts)
    {
        return new MultipartMessageCollection(nativeParts ?? Array.Empty<ZlinkMsg>());
    }

    public int Count => _messages.Length;

    public Message this[int index]
    {
        get
        {
            if (IsDisposed)
                throw new ObjectDisposedException(nameof(MultipartMessageCollection));
            if ((uint)index >= (uint)_messages.Length)
                throw new ArgumentOutOfRangeException(nameof(index));
            return GetOrMaterialize(index);
        }
    }

    internal bool IsSinglePart => Count == 1;

    internal Message First()
    {
        if (Count == 0)
        {
            throw new ZlinkRecvException(RecvResult.NoData,
                (int)ErrorCode.EAgain);
        }

        return this[0];
    }

    internal Message Single()
    {
        if (Count != 1)
        {
            throw new ZlinkRecvException(RecvResult.NotSupported,
                (int)ErrorCode.ENotSup);
        }

        return this[0];
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;

        lock (_gate)
        {
            for (int i = 0; i < _messages.Length; i++)
                _messages[i]?.Dispose();

            if (_nativeParts == null)
                return;

            for (int i = 0; i < _nativeParts.Length; i++)
                NativeMethods.zlink_msg_close(ref _nativeParts[i]);
            _nativeParts = null;
        }
    }

    public IEnumerator<Message> GetEnumerator()
    {
        if (IsDisposed)
            throw new ObjectDisposedException(nameof(MultipartMessageCollection));
        for (int i = 0; i < _messages.Length; i++)
            yield return GetOrMaterialize(i);
    }

    IEnumerator IEnumerable.GetEnumerator()
    {
        return GetEnumerator();
    }

    private bool IsDisposed => Volatile.Read(ref _closed) != 0;

    private Message GetOrMaterialize(int index)
    {
        Message? existing = Volatile.Read(ref _messages[index]);
        if (existing != null)
            return existing;

        lock (_gate)
        {
            existing = _messages[index];
            if (existing != null)
                return existing;

            if (_nativeParts == null)
                throw new ObjectDisposedException(nameof(MultipartMessageCollection));

            Message created = Message.MoveFromNative(ref _nativeParts[index]);
            _messages[index] = created;
            if (AllMaterialized())
                _nativeParts = null;
            return created;
        }
    }

    private bool AllMaterialized()
    {
        for (int i = 0; i < _messages.Length; i++)
        {
            if (_messages[i] == null)
                return false;
        }

        return true;
    }
}

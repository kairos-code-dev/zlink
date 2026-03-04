using System;
using System.Collections.Generic;
using System.Threading;
using Zlink;

internal sealed class PerfStreamCallbackEcho : IDisposable
{
    private const int MaxFrameBytes = 16 * 1024 * 1024;
    private readonly Zlink.Socket _socket;
    private readonly bool _len32be;
    private readonly bool _echo;
    private readonly object _stashLock = new();
    private byte[] _stash = new byte[512];
    private int _stashStart;
    private int _stashEnd;
    private long _received;

    internal PerfStreamCallbackEcho(Zlink.Socket socket, bool len32be,
        bool echo = true)
    {
        _socket = socket ?? throw new ArgumentNullException(nameof(socket));
        _len32be = len32be;
        _echo = echo;
    }

    internal long Received => Interlocked.Read(ref _received);

    internal static byte[] WaitPeerRoutingId(Zlink.Socket socket, int timeoutMs)
    {
        if (socket == null)
            throw new ArgumentNullException(nameof(socket));
        var deadline = DateTime.UtcNow.AddMilliseconds(Math.Max(timeoutMs, 1));
        while (DateTime.UtcNow < deadline)
        {
            byte[]? rid = socket.GetPeerRoutingId(0);
            if (rid != null && rid.Length == 4)
                return rid;
            Thread.Sleep(1);
        }
        throw new TimeoutException("STREAM peer routing id unavailable");
    }

    internal static void Send(Zlink.Socket socket, uint rid,
        ReadOnlySpan<byte> payload)
    {
        if (socket == null)
            throw new ArgumentNullException(nameof(socket));
        socket.StreamSend(rid, payload);
    }

    internal void Attach()
    {
        if (_len32be)
            _socket.AttachStreamLen32Be(OnPacketBatch);
        else
            _socket.AttachStreamRaw(OnPacketRaw);
    }

    internal void Detach()
    {
        _socket.DetachStream();
    }

    private int OnPacketRaw(uint rid, Message message)
    {
        ReadOnlySpan<byte> payload = message.AsReadOnlySpan();
        int payloadSize = payload.Length;
        if (payloadSize <= 0)
        {
            message.Dispose();
            return 0;
        }
        if (payloadSize == 1 && (payload[0] == 0x00 || payload[0] == 0x01))
        {
            message.Dispose();
            return 0;
        }

        if (_echo)
        {
            _socket.StreamSend(rid, message);
            return 0;
        }

        try
        {
            List<byte[]> frames = ExtractRawFrames(payload);
            for (int f = 0; f < frames.Count; f++)
            {
                byte[] frame = frames[f];
                if (frame.Length == 0)
                    continue;
                Interlocked.Increment(ref _received);
            }
            return 0;
        }
        finally
        {
            message.Dispose();
        }
    }

    private int OnPacketBatch(uint rid, Message[] messages)
    {
        for (int i = 0; i < messages.Length; i++)
        {
            Message message = messages[i];
            ReadOnlySpan<byte> payload = message.AsReadOnlySpan();
            int payloadSize = payload.Length;
            if (payloadSize <= 0)
            {
                message.Dispose();
                continue;
            }
            if (payloadSize == 1 && (payload[0] == 0x00 || payload[0] == 0x01))
            {
                message.Dispose();
                continue;
            }

            if (_echo)
            {
                int sent = _socket.StreamSend(rid, message);
                if (sent == payloadSize)
                    Interlocked.Increment(ref _received);
            }
            else
            {
                Interlocked.Increment(ref _received);
                message.Dispose();
            }
        }
        return 0;
    }

    private List<byte[]> ExtractRawFrames(ReadOnlySpan<byte> chunk)
    {
        List<byte[]> frames = new();
        lock (_stashLock)
        {
            AppendToStash(chunk);
            while (TryExtractRawFrame(out byte[]? frame))
            {
                if (frame is null)
                    continue;
                frames.Add(frame);
            }
        }
        return frames;
    }

    private void AppendToStash(ReadOnlySpan<byte> chunk)
    {
        if (chunk.Length == 0)
            return;
        EnsureStashCapacity(chunk.Length);
        chunk.CopyTo(_stash.AsSpan(_stashEnd));
        _stashEnd += chunk.Length;
    }

    private bool TryExtractRawFrame(out byte[]? frame)
    {
        frame = null;
        int available = _stashEnd - _stashStart;
        if (available < 4)
            return false;

        int s = _stashStart;
        int bodyLen = (_stash[s] << 24)
            | (_stash[s + 1] << 16)
            | (_stash[s + 2] << 8)
            | _stash[s + 3];
        if (bodyLen < 0 || bodyLen > MaxFrameBytes)
        {
            _stashStart = 0;
            _stashEnd = 0;
            return false;
        }

        int frameLen = 4 + bodyLen;
        if (available < frameLen)
            return false;

        frame = GC.AllocateUninitializedArray<byte>(frameLen);
        Buffer.BlockCopy(_stash, _stashStart, frame, 0, frameLen);
        _stashStart += frameLen;

        if (_stashStart >= _stashEnd)
        {
            _stashStart = 0;
            _stashEnd = 0;
        }
        else if (_stashStart > 4096 && (_stashStart * 2) >= _stashEnd)
        {
            int remain = _stashEnd - _stashStart;
            Buffer.BlockCopy(_stash, _stashStart, _stash, 0, remain);
            _stashStart = 0;
            _stashEnd = remain;
        }

        return true;
    }

    private void EnsureStashCapacity(int appendBytes)
    {
        int spare = _stash.Length - _stashEnd;
        if (spare >= appendBytes)
            return;

        int used = _stashEnd - _stashStart;
        if (_stashStart > 0)
        {
            if (used > 0)
                Buffer.BlockCopy(_stash, _stashStart, _stash, 0, used);
            _stashStart = 0;
            _stashEnd = used;
            spare = _stash.Length - _stashEnd;
            if (spare >= appendBytes)
                return;
        }

        int needed = _stashEnd + appendBytes;
        int next = Math.Max(needed, _stash.Length * 2);
        Array.Resize(ref _stash, next);
    }

    public void Dispose()
    {
        try
        {
            Detach();
        }
        catch
        {
        }
    }
}

using System;

internal static partial class PerfRunner
{
    private sealed class Len32StopTokenParser
    {
        private byte[] _buffer = new byte[2048];
        private int _start;
        private int _end;

        internal bool Consume(ReadOnlySpan<byte> chunk)
        {
            if (chunk.Length <= 0)
                return false;

            EnsureCapacity(chunk.Length);
            chunk.CopyTo(_buffer.AsSpan(_end));
            _end += chunk.Length;

            bool found = false;
            while ((_end - _start) >= 4)
            {
                int bodyLen = (_buffer[_start] << 24)
                    | (_buffer[_start + 1] << 16)
                    | (_buffer[_start + 2] << 8)
                    | _buffer[_start + 3];
                if (bodyLen < 0 || bodyLen > MaxStreamFrameBytes)
                {
                    _start = 0;
                    _end = 0;
                    return false;
                }

                int frameLen = 4 + bodyLen;
                if ((_end - _start) < frameLen)
                    break;

                if (bodyLen == MultiStopToken.Length
                    && _buffer.AsSpan(_start + 4, bodyLen)
                        .SequenceEqual(MultiStopToken))
                {
                    found = true;
                }

                _start += frameLen;
            }

            Compact();
            return found;
        }

        private void EnsureCapacity(int incoming)
        {
            int needed = _end + incoming;
            if (needed <= _buffer.Length)
                return;

            Compact();
            needed = _end + incoming;
            if (needed <= _buffer.Length)
                return;

            int next = _buffer.Length;
            while (next < needed)
                next *= 2;

            byte[] grown = new byte[next];
            int remain = _end - _start;
            if (remain > 0)
                Buffer.BlockCopy(_buffer, _start, grown, 0, remain);
            _buffer = grown;
            _start = 0;
            _end = remain;
        }

        private void Compact()
        {
            if (_start <= 0)
                return;
            if (_start >= _end)
            {
                _start = 0;
                _end = 0;
                return;
            }

            int remain = _end - _start;
            Buffer.BlockCopy(_buffer, _start, _buffer, 0, remain);
            _start = 0;
            _end = remain;
        }
    }
}

using System;
using System.IO;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Threading;

internal static partial class PerfRunner
{
    private sealed class RawTransportStreamClient : IDisposable
    {
        private readonly Uri _uri;
        private readonly int _sendTimeoutMs;
        private readonly int _recvTimeoutMs;
        private readonly string _scheme;

        private System.Net.Sockets.Socket? _tcpSocket;
        private NetworkStream? _networkStream;
        private SslStream? _tlsStream;
        private ClientWebSocket? _webSocket;

        private byte[] _wsStash = new byte[8192];
        private int _wsStashStart;
        private int _wsStashEnd;

        internal RawTransportStreamClient(string endpoint, int sendTimeoutMs,
            int recvTimeoutMs)
        {
            if (!Uri.TryCreate(endpoint, UriKind.Absolute, out var uri)
                || uri.Port <= 0)
            {
                throw new ArgumentException("invalid stream endpoint", nameof(endpoint));
            }

            string scheme = uri.Scheme.ToLowerInvariant();
            if (scheme != "tcp" && scheme != "tls" && scheme != "ws"
                && scheme != "wss")
            {
                throw new ArgumentException("unsupported stream transport",
                    nameof(endpoint));
            }

            _uri = uri;
            _scheme = scheme;
            _sendTimeoutMs = Math.Max(1, sendTimeoutMs);
            _recvTimeoutMs = Math.Max(1, recvTimeoutMs);
        }

        internal void Connect()
        {
            if (_scheme == "ws" || _scheme == "wss")
            {
                ConnectWebSocket();
                return;
            }
            ConnectTcpLike();
        }

        internal void SendFrame(ReadOnlySpan<byte> payload)
        {
            byte[] frame = EncodeLen32Be(payload);
            if (_webSocket != null)
            {
                using var cts = new CancellationTokenSource(_sendTimeoutMs);
                _webSocket.SendAsync(frame, WebSocketMessageType.Binary, true,
                    cts.Token).GetAwaiter().GetResult();
                return;
            }

            Stream stream = GetNetworkStream();
            WriteAll(stream, frame.AsSpan());
        }

        internal byte[] ReceiveFramePayload()
        {
            if (_webSocket != null)
                return ReceiveWebSocketPayload();

            Stream stream = GetNetworkStream();
            byte[] header = new byte[4];
            ReadExact(stream, header.AsSpan());
            int bodyLen = DecodeFrameLength(header);
            byte[] payload = new byte[bodyLen];
            ReadExact(stream, payload.AsSpan());
            return payload;
        }

        public void Dispose()
        {
            try
            {
                if (_webSocket != null)
                {
                    if (_webSocket.State == WebSocketState.Open)
                    {
                        using var cts = new CancellationTokenSource(_sendTimeoutMs);
                        _webSocket.CloseAsync(WebSocketCloseStatus.NormalClosure,
                            "done", cts.Token).GetAwaiter().GetResult();
                    }
                    _webSocket.Dispose();
                }
            }
            catch
            {
            }

            try { _tlsStream?.Dispose(); } catch { }
            try { _networkStream?.Dispose(); } catch { }
            try { _tcpSocket?.Dispose(); } catch { }
        }

        private void ConnectTcpLike()
        {
            _tcpSocket = new System.Net.Sockets.Socket(
                AddressFamily.InterNetwork,
                System.Net.Sockets.SocketType.Stream,
                ProtocolType.Tcp);
            _tcpSocket.NoDelay = true;
            _tcpSocket.SendTimeout = _sendTimeoutMs;
            _tcpSocket.ReceiveTimeout = _recvTimeoutMs;
            _tcpSocket.Connect(_uri.Host, _uri.Port);
            _networkStream = new NetworkStream(_tcpSocket, ownsSocket: false);
            _networkStream.ReadTimeout = _recvTimeoutMs;
            _networkStream.WriteTimeout = _sendTimeoutMs;

            if (_scheme == "tls")
            {
                _tlsStream = new SslStream(_networkStream, false,
                    static (_, _, _, _) => true);
                _tlsStream.ReadTimeout = _recvTimeoutMs;
                _tlsStream.WriteTimeout = _sendTimeoutMs;
                _tlsStream.AuthenticateAsClient(_uri.Host);
            }
        }

        private void ConnectWebSocket()
        {
            _webSocket = new ClientWebSocket();
            _webSocket.Options.KeepAliveInterval = TimeSpan.FromSeconds(20);
            if (_scheme == "wss")
            {
                _webSocket.Options.RemoteCertificateValidationCallback =
                    static (_, _, _, _) => true;
            }

            Uri connectUri = NormalizeWebSocketUri(_uri);
            using var cts = new CancellationTokenSource(
                Math.Max(_sendTimeoutMs, _recvTimeoutMs));
            _webSocket.ConnectAsync(connectUri, cts.Token).GetAwaiter().GetResult();
        }

        private static Uri NormalizeWebSocketUri(Uri source)
        {
            string path = string.IsNullOrEmpty(source.AbsolutePath)
                ? "/"
                : source.AbsolutePath;
            var builder = new UriBuilder(source)
            {
                Path = path,
            };
            return builder.Uri;
        }

        private Stream GetNetworkStream()
        {
            if (_tlsStream != null)
                return _tlsStream;
            if (_networkStream != null)
                return _networkStream;
            throw new InvalidOperationException("stream client is not connected");
        }

        private byte[] ReceiveWebSocketPayload()
        {
            while (true)
            {
                if (TryConsumeWsFrame(out byte[]? payload))
                {
                    if (payload != null)
                        return payload;
                    throw new InvalidOperationException("websocket payload missing");
                }

                byte[] message = ReceiveWebSocketMessage();
                AppendWsStash(message);
            }
        }

        private byte[] ReceiveWebSocketMessage()
        {
            if (_webSocket == null)
                throw new InvalidOperationException("websocket is not connected");

            var buffer = new byte[8192];
            using var ms = new MemoryStream();
            while (true)
            {
                using var cts = new CancellationTokenSource(_recvTimeoutMs);
                WebSocketReceiveResult result = _webSocket.ReceiveAsync(buffer,
                    cts.Token).GetAwaiter().GetResult();
                if (result.MessageType == WebSocketMessageType.Close)
                    throw new InvalidOperationException("websocket closed");
                if (result.Count > 0)
                    ms.Write(buffer, 0, result.Count);
                if (result.EndOfMessage)
                    break;
            }
            return ms.ToArray();
        }

        private bool TryConsumeWsFrame(out byte[]? payload)
        {
            payload = null;
            int available = _wsStashEnd - _wsStashStart;
            if (available < 4)
                return false;

            int bodyLen = (_wsStash[_wsStashStart] << 24)
                | (_wsStash[_wsStashStart + 1] << 16)
                | (_wsStash[_wsStashStart + 2] << 8)
                | _wsStash[_wsStashStart + 3];
            if (bodyLen < 0 || bodyLen > MaxStreamFrameBytes)
            {
                _wsStashStart = 0;
                _wsStashEnd = 0;
                throw new InvalidOperationException("invalid websocket frame size");
            }

            int frameLen = 4 + bodyLen;
            if (available < frameLen)
                return false;

            payload = new byte[bodyLen];
            if (bodyLen > 0)
            {
                Buffer.BlockCopy(_wsStash, _wsStashStart + 4, payload, 0, bodyLen);
            }
            _wsStashStart += frameLen;
            CompactWsStash();
            return true;
        }

        private void AppendWsStash(ReadOnlySpan<byte> chunk)
        {
            EnsureWsCapacity(chunk.Length);
            chunk.CopyTo(_wsStash.AsSpan(_wsStashEnd));
            _wsStashEnd += chunk.Length;
        }

        private void EnsureWsCapacity(int incoming)
        {
            int needed = _wsStashEnd + incoming;
            if (needed <= _wsStash.Length)
                return;

            CompactWsStash();
            needed = _wsStashEnd + incoming;
            if (needed <= _wsStash.Length)
                return;

            int next = _wsStash.Length;
            while (next < needed)
                next *= 2;
            Array.Resize(ref _wsStash, next);
        }

        private void CompactWsStash()
        {
            if (_wsStashStart <= 0)
                return;
            if (_wsStashStart >= _wsStashEnd)
            {
                _wsStashStart = 0;
                _wsStashEnd = 0;
                return;
            }

            int remain = _wsStashEnd - _wsStashStart;
            Buffer.BlockCopy(_wsStash, _wsStashStart, _wsStash, 0, remain);
            _wsStashStart = 0;
            _wsStashEnd = remain;
        }

        private static byte[] EncodeLen32Be(ReadOnlySpan<byte> payload)
        {
            byte[] frame = GC.AllocateUninitializedArray<byte>(payload.Length + 4);
            int size = payload.Length;
            frame[0] = (byte)((size >> 24) & 0xFF);
            frame[1] = (byte)((size >> 16) & 0xFF);
            frame[2] = (byte)((size >> 8) & 0xFF);
            frame[3] = (byte)(size & 0xFF);
            payload.CopyTo(frame.AsSpan(4));
            return frame;
        }

        private static int DecodeFrameLength(ReadOnlySpan<byte> header)
        {
            int bodyLen = (header[0] << 24)
                | (header[1] << 16)
                | (header[2] << 8)
                | header[3];
            if (bodyLen < 0 || bodyLen > MaxStreamFrameBytes)
            {
                throw new InvalidOperationException("invalid stream frame size");
            }
            return bodyLen;
        }

        private static void WriteAll(Stream stream, ReadOnlySpan<byte> payload)
        {
            int offset = 0;
            while (offset < payload.Length)
            {
                int remaining = payload.Length - offset;
                stream.Write(payload.Slice(offset, remaining));
                offset += remaining;
            }
            stream.Flush();
        }

        private static void ReadExact(Stream stream, Span<byte> buffer)
        {
            int offset = 0;
            while (offset < buffer.Length)
            {
                int n = stream.Read(buffer.Slice(offset));
                if (n <= 0)
                    throw new InvalidOperationException("stream receive failed");
                offset += n;
            }
        }
    }
}

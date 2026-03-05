using System;
using System.Diagnostics;
using System.IO;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    private enum StreamServerMode
    {
        BasicRecv,
        Callback,
        CallbackLen32Be,
    }

    private static readonly byte[] StreamStopToken =
        System.Text.Encoding.ASCII.GetBytes("__zlink_perf_stop__");

    private const int MaxStreamFrameBytes = 16 * 1024 * 1024;

    private static int ParsePerfOnly(string name, int defaultValue)
    {
        string? raw = Environment.GetEnvironmentVariable(name);
        if (string.IsNullOrWhiteSpace(raw))
            return defaultValue;
        return int.TryParse(raw, out int parsed) && parsed > 0
            ? parsed
            : defaultValue;
    }

    internal static int RunStream(string transport, int size)
    {
        return RunStreamWithRawTransportClient(transport, size, "STREAM",
            StreamServerMode.BasicRecv);
    }

    internal static int RunStreamCallback(string transport, int size)
    {
        return RunStreamWithRawTransportClient(transport, size, "STREAM_CALLBACK",
            StreamServerMode.Callback);
    }

    internal static int RunStreamLen32Be(string transport, int size)
    {
        return RunStreamWithRawTransportClient(transport, size, "STREAM_LEN32BE",
            StreamServerMode.CallbackLen32Be);
    }

    private static int RunStreamWithRawTransportClient(string transport, int size,
        string patternName, StreamServerMode serverMode)
    {
        int warmupSeconds = ParseEnv("PERF_SINGLE_WARMUP_SECONDS", 3);
        int settleMs = ParseEnv("PERF_SINGLE_SETTLE_MS", 300);
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnv("PERF_SINGLE_DRAIN_MS", 300);
        int latCount = ParseEnv("PERF_LAT_COUNT", 500);
        int ioTimeoutMs = ParsePerfOnly("PERF_STREAM_TIMEOUT_MS", 5000);

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Stream);
        ApplySingleSocketOptions(server);
        int stopRequested = 0;
        int serverFailed = 0;

        try
        {
            string endpoint = EndpointFor(transport, patternName.ToLowerInvariant());
            ConfigureStreamServerTlsIfNeeded(server, transport);
            server.SetOption(SocketOptions.SndTimeo, ioTimeoutMs);
            server.SetOption(SocketOptions.RcvTimeo, ioTimeoutMs);

            Len32StopTokenParser stopParser = new Len32StopTokenParser();
            bool len32beMode = serverMode == StreamServerMode.CallbackLen32Be;
            StreamPacketHandler rawHandler = (rid, payload) =>
            {
                ReadOnlySpan<byte> payloadBytes = payload.AsReadOnlySpan();
                if (payloadBytes.Length == 1
                    && (payloadBytes[0] == 0x00 || payloadBytes[0] == 0x01))
                {
                    payload.Dispose();
                    return 0;
                }

                bool isStop = stopParser.Consume(payloadBytes);
                if (isStop)
                {
                    Interlocked.Exchange(ref stopRequested, 1);
                    payload.Dispose();
                    return 0;
                }

                try
                {
                    server.StreamSend(rid, payload, SendFlags.None);
                }
                catch
                {
                    Interlocked.Exchange(ref serverFailed, 1);
                    Interlocked.Exchange(ref stopRequested, 1);
                }
                return 0;
            };

            StreamBatchHandler len32BeHandler = (rid, messages) =>
            {
                for (int i = 0; i < messages.Length; i++)
                {
                    Message message = messages[i];
                    ReadOnlySpan<byte> payload = message.AsReadOnlySpan();
                    if (payload.Length == 1
                        && (payload[0] == 0x00 || payload[0] == 0x01))
                    {
                        message.Dispose();
                        continue;
                    }

                    if (payload.SequenceEqual(StreamStopToken))
                    {
                        Interlocked.Exchange(ref stopRequested, 1);
                        message.Dispose();
                        continue;
                    }

                    try
                    {
                        server.StreamSend(rid, message, SendFlags.None);
                    }
                    catch
                    {
                        Interlocked.Exchange(ref serverFailed, 1);
                        Interlocked.Exchange(ref stopRequested, 1);
                    }
                }
                return 0;
            };

            if (len32beMode)
                server.AttachStreamLen32Be(len32BeHandler);
            else
                server.AttachStreamRaw(rawHandler);

            server.Bind(endpoint);

            using var client = new RawTransportStreamClient(endpoint, ioTimeoutMs);
            client.Connect();
            Thread.Sleep(200);

            var payloadBody = new byte[Math.Max(1, size)];
            Array.Fill(payloadBody, (byte)'a');

            var warmupDeadline = DateTime.UtcNow.AddSeconds(Math.Max(0,
                warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                client.SendFrame(payloadBody);
                _ = client.ReceiveFramePayload();
            }

            Thread.Sleep(Math.Max(0, settleMs));

            int recvCount = 0;
            var sw = Stopwatch.StartNew();
            var throughputDeadline = DateTime.UtcNow.AddSeconds(Math.Max(1,
                durationSeconds));
            while (DateTime.UtcNow < throughputDeadline)
            {
                client.SendFrame(payloadBody);
                _ = client.ReceiveFramePayload();
                recvCount++;
            }
            sw.Stop();

            double throughput = (recvCount > 0 && sw.Elapsed.TotalSeconds > 0.0)
                ? recvCount / sw.Elapsed.TotalSeconds
                : 0.0;

            Thread.Sleep(Math.Max(0, drainMs));

            sw.Restart();
            for (int i = 0; i < latCount; i++)
            {
                client.SendFrame(payloadBody);
                _ = client.ReceiveFramePayload();
            }
            sw.Stop();
            double latencyUs = (sw.Elapsed.TotalMilliseconds * 1000.0)
                / Math.Max(1, latCount * 2);

            try
            {
                client.SendFrame(StreamStopToken);
            }
            catch
            {
            }
            var stopDeadline = DateTime.UtcNow.AddMilliseconds(
                Math.Max(1000, ioTimeoutMs));
            while (DateTime.UtcNow < stopDeadline
                   && Volatile.Read(ref stopRequested) == 0)
            {
                Thread.Sleep(1);
            }
            if (Volatile.Read(ref stopRequested) == 0)
                return 2;

            if (Volatile.Read(ref serverFailed) != 0)
                return 2;

            PrintResult(patternName, transport, size, throughput, latencyUs);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"stream_single_error:{ex.Message}");
            return 2;
        }
        finally
        {
            Interlocked.Exchange(ref stopRequested, 1);
            try
            {
                server.DetachStream();
            }
            catch
            {
            }
        }
    }

    private static void ConfigureStreamServerTlsIfNeeded(Zlink.Socket server,
        string transport)
    {
        if (!transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            && !transport.Equals("wss", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        if (!TryResolvePerfTlsPaths(out string certPath, out string keyPath,
                out _))
        {
            throw new InvalidOperationException(
                "TLS certificate files not found under bindings/dotnet/tests/certs");
        }

        server.SetOption(SocketOptions.TlsCert, certPath);
        server.SetOption(SocketOptions.TlsKey, keyPath);
    }

    private static bool TryResolvePerfTlsPaths(out string certPath,
        out string keyPath, out string caPath)
    {
        certPath = string.Empty;
        keyPath = string.Empty;
        caPath = string.Empty;

        string[] roots =
        {
            AppContext.BaseDirectory,
            Directory.GetCurrentDirectory(),
        };

        foreach (string start in roots)
        {
            var dir = new DirectoryInfo(start);
            while (dir != null)
            {
                string[] certDirs =
                {
                    Path.Combine(dir.FullName, "bindings", "dotnet", "tests",
                        "certs"),
                    Path.Combine(dir.FullName, "tests", "certs"),
                };
                foreach (string certDir in certDirs)
                {
                    string cert = Path.Combine(certDir, "server.crt");
                    string key = Path.Combine(certDir, "server.key");
                    string ca = Path.Combine(certDir, "ca.crt");
                    if (File.Exists(cert) && File.Exists(key) && File.Exists(ca))
                    {
                        certPath = cert;
                        keyPath = key;
                        caPath = ca;
                        return true;
                    }
                }
                dir = dir.Parent;
            }
        }

        return false;
    }

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

                if (bodyLen == StreamStopToken.Length
                    && _buffer.AsSpan(_start + 4, bodyLen)
                        .SequenceEqual(StreamStopToken))
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

    private sealed class RawTransportStreamClient : IDisposable
    {
        private readonly Uri _uri;
        private readonly int _timeoutMs;
        private readonly string _scheme;

        private System.Net.Sockets.Socket? _tcpSocket;
        private NetworkStream? _networkStream;
        private SslStream? _tlsStream;
        private ClientWebSocket? _webSocket;

        private byte[] _wsStash = new byte[8192];
        private int _wsStashStart;
        private int _wsStashEnd;

        internal RawTransportStreamClient(string endpoint, int timeoutMs)
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
            _timeoutMs = Math.Max(1, timeoutMs);
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
                using var cts = new CancellationTokenSource(_timeoutMs);
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
                        using var cts = new CancellationTokenSource(_timeoutMs);
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
            _tcpSocket.SendTimeout = _timeoutMs;
            _tcpSocket.ReceiveTimeout = _timeoutMs;
            _tcpSocket.Connect(_uri.Host, _uri.Port);
            _networkStream = new NetworkStream(_tcpSocket, ownsSocket: false);
            _networkStream.ReadTimeout = _timeoutMs;
            _networkStream.WriteTimeout = _timeoutMs;

            if (_scheme == "tls")
            {
                _tlsStream = new SslStream(_networkStream, false,
                    static (_, _, _, _) => true);
                _tlsStream.ReadTimeout = _timeoutMs;
                _tlsStream.WriteTimeout = _timeoutMs;
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
            using var cts = new CancellationTokenSource(_timeoutMs);
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
                using var cts = new CancellationTokenSource(_timeoutMs);
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

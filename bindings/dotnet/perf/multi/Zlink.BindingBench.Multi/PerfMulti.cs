using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    private static readonly byte[] MultiStopToken =
        System.Text.Encoding.ASCII.GetBytes("__zlink_perf_stop__");

    private const int MaxStreamFrameBytes = 16 * 1024 * 1024;

    private static bool IsMultiStreamPattern(string pattern)
    {
        return pattern.Equals("MULTI_STREAM", StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_CALLBACK",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_LEN32BE",
                StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsMultiStreamCallbackPattern(string pattern)
    {
        return pattern.Equals("MULTI_STREAM_CALLBACK",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_LEN32BE",
                StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsMultiLen32BePattern(string pattern)
    {
        return pattern.Equals("MULTI_STREAM_LEN32BE",
            StringComparison.OrdinalIgnoreCase);
    }

    private static int ResolveMultiDrainMs(string pattern)
    {
        int defaultDrain = pattern.Equals("MULTI_GATEWAY",
                               StringComparison.OrdinalIgnoreCase)
                               || pattern.Equals("MULTI_SPOT",
                                   StringComparison.OrdinalIgnoreCase)
            ? 0
            : 300;
        return ParseNonNegativeEnv("PERF_MULTI_DRAIN_MS", defaultDrain);
    }

    private static string MultiEndpointFor(string transport, string name)
    {
        int bindPort = ParseNonNegativeEnv("PERF_MULTI_SERVER_BIND_PORT", 0);
        if (bindPort > 0)
            return $"{transport}://127.0.0.1:{bindPort}";
        return EndpointFor(transport, name);
    }

    private static bool IsMonitorReady(ulong eventValue, bool acceptFallback)
    {
        if (eventValue == (ulong)SocketEvent.ConnectionReady)
            return true;
        if (!acceptFallback)
            return false;
        return eventValue == (ulong)SocketEvent.Accepted
            || eventValue == (ulong)SocketEvent.Connected;
    }

    private static bool WaitMonitorReady(MonitorSocket monitor, int timeoutMs,
        bool acceptFallback)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(Math.Max(1, timeoutMs));
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                MonitorEvent evt = monitor.Receive(ReceiveFlags.DontWait);
                if (IsMonitorReady(evt.Event, acceptFallback))
                    return true;
            }
            catch (ZlinkException ex) when (ex.Errno == ErrnoEagain
                                            || ex.Errno == ErrnoEintr)
            {
                Thread.Sleep(1);
            }
            catch
            {
                return false;
            }
        }
        return false;
    }

    private static Zlink.SocketType ResolveServerSocketType(string pattern)
    {
        if (pattern.Equals("MULTI_DEALER_ROUTER", StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_ROUTER_ROUTER",
                StringComparison.OrdinalIgnoreCase))
        {
            return Zlink.SocketType.Router;
        }
        if (pattern.Equals("MULTI_PUBSUB", StringComparison.OrdinalIgnoreCase))
            return Zlink.SocketType.Sub;
        return Zlink.SocketType.Dealer;
    }

    private static Zlink.SocketType ResolveClientSocketType(string pattern)
    {
        if (pattern.Equals("MULTI_PUBSUB", StringComparison.OrdinalIgnoreCase))
            return Zlink.SocketType.Pub;
        return Zlink.SocketType.Dealer;
    }

    private static bool IsStopTokenPayload(ReadOnlySpan<byte> payload)
    {
        return payload.Length == MultiStopToken.Length
            && payload.SequenceEqual(MultiStopToken);
    }

    internal static int RunMultiServer(string pattern, string transport, int size)
    {
        size = Math.Max(1, size);
        int rcvTimeoutMs = ParsePositiveEnv("PERF_MULTI_RCVTIMEO_MS", 5000);
        int readyTimeoutMs =
            ParsePositiveEnv("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);
        string endpoint = MultiEndpointFor(transport, $"multi-{pattern}");

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();

        using var ctx = new Context();
        bool streamPattern = IsMultiStreamPattern(pattern);

        try
        {
            if (streamPattern)
            {
                using var server = new Zlink.Socket(ctx, Zlink.SocketType.Stream);
                ConfigureTlsServerIfNeeded(server, transport);
                server.SetOption(SocketOption.RcvTimeo, rcvTimeoutMs);

                int stopRequested = 0;
                int callbackFailed = 0;
                long payloadSeen = 0;
                long lastActivityTicks = Stopwatch.GetTimestamp();
                StreamDispatchMode dispatchMode = IsMultiLen32BePattern(pattern)
                    ? StreamDispatchMode.Len32Be
                    : StreamDispatchMode.None;
                var stopParser = new Len32StopTokenParser();
                server.AttachStream((rid, payload) =>
                {
                    if (payload.Length == 1
                        && (payload[0] == 0x00 || payload[0] == 0x01))
                    {
                        return 0;
                    }

                    bool stopDetected = dispatchMode == StreamDispatchMode.Len32Be
                        ? payload.SequenceEqual(MultiStopToken)
                        : stopParser.Consume(payload);
                    if (stopDetected)
                    {
                        Interlocked.Exchange(ref stopRequested, 1);
                        return 0;
                    }
                    Interlocked.Increment(ref payloadSeen);
                    Interlocked.Exchange(ref lastActivityTicks,
                        Stopwatch.GetTimestamp());

                    try
                    {
                        server.StreamSend(rid, payload, SendFlags.None);
                    }
                    catch
                    {
                        Interlocked.Exchange(ref callbackFailed, 1);
                        Interlocked.Exchange(ref stopRequested, 1);
                    }
                    return 0;
                }, dispatchMode);

                server.Bind(endpoint);
                Console.WriteLine($"READY,{endpoint}");
                long idleBreakTicks = (long)(
                    Stopwatch.Frequency
                    * (Math.Max(rcvTimeoutMs * 2, 5000) / 1000.0));

                while (Volatile.Read(ref stopRequested) == 0)
                {
                    if (Volatile.Read(ref callbackFailed) != 0)
                        return 2;
                    if (Volatile.Read(ref payloadSeen) > 0)
                    {
                        long idleTicks = Stopwatch.GetTimestamp()
                            - Volatile.Read(ref lastActivityTicks);
                        if (idleTicks >= idleBreakTicks)
                            break;
                    }
                    Thread.Sleep(1);
                }

                try
                {
                    server.DetachStream();
                }
                catch
                {
                }

                if (Volatile.Read(ref callbackFailed) != 0)
                    return 2;
            }
            else
            {
                Zlink.SocketType serverType = ResolveServerSocketType(pattern);
                using var server = new Zlink.Socket(ctx, serverType);
                ConfigureTlsServerIfNeeded(server, transport);
                if (serverType == Zlink.SocketType.Sub)
                {
                    server.SetOption(SocketOption.Subscribe,
                        ReadOnlySpan<byte>.Empty);
                }

                using var monitor = server.MonitorOpen(
                    SocketEvent.ConnectionReady
                    | SocketEvent.Accepted
                    | SocketEvent.Connected);

                server.SetOption(SocketOption.RcvTimeo, rcvTimeoutMs);
                server.Bind(endpoint);
                Console.WriteLine($"READY,{endpoint}");

                if (!WaitMonitorReady(monitor, readyTimeoutMs, true))
                    return 2;

                bool echoMode = IsEchoPattern(pattern);
                var routingId = new byte[256];
                var payload =
                    new byte[Math.Max(256, Math.Max(size, MultiStopToken.Length))];

                while (true)
                {
                    if (serverType == Zlink.SocketType.Router)
                    {
                        int ridLen = ReceiveRetry(server, routingId.AsSpan(),
                            ReceiveFlags.None);
                        int n = ReceiveRetry(server, payload.AsSpan(),
                            ReceiveFlags.None);
                        if (n <= 0)
                            continue;

                        ReadOnlySpan<byte> body = payload.AsSpan(0, n);
                        if (IsStopTokenPayload(body))
                            break;

                        if (echoMode)
                        {
                            SendRetry(server, routingId.AsSpan(0, ridLen),
                                SendFlags.SendMore);
                            SendRetry(server, body, SendFlags.None);
                        }
                    }
                    else
                    {
                        int n = ReceiveRetry(server, payload.AsSpan(),
                            ReceiveFlags.None);
                        if (n <= 0)
                            continue;

                        ReadOnlySpan<byte> body = payload.AsSpan(0, n);
                        if (IsStopTokenPayload(body))
                            break;

                        if (echoMode)
                            SendRetry(server, body, SendFlags.None);
                    }
                }
            }

            wall.Stop();
            process.Refresh();
            TimeSpan cpuEnd = process.TotalProcessorTime;
            double cpuSec = Math.Max(0.0, (cpuEnd - cpuStart).TotalSeconds);
            double wallSec = Math.Max(1e-9, wall.Elapsed.TotalSeconds);
            double ncpu = Math.Max(1, Environment.ProcessorCount);
            double cpuPct = (cpuSec / (wallSec * ncpu)) * 100.0;
            double memMb = process.WorkingSet64 / (1024.0 * 1024.0);

            Console.WriteLine(
                $"RESULT,current,{pattern},{transport},{size},server_cpu_pct,{cpuPct}");
            Console.WriteLine(
                $"RESULT,current,{pattern},{transport},{size},server_mem_mb,{memMb}");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"multi_server_error:{ex.Message}");
            return 2;
        }
    }

    internal static int RunMultiClient(string pattern, string transport, int size,
        string endpoint)
    {
        size = Math.Max(1, size);
        int warmupSeconds = ParseNonNegativeEnv("PERF_MULTI_WARMUP_SECONDS", 3);
        int durationSeconds = ParsePositiveEnv("PERF_MULTI_DURATION_SECONDS", 5);
        int settleMs = ParseNonNegativeEnv("PERF_MULTI_SETTLE_MS", 500);
        int drainMs = ResolveMultiDrainMs(pattern);
        int sizeTransitionDrainMs = ParseNonNegativeEnv(
            "PERF_MULTI_SIZE_TRANSITION_DRAIN_MS", 300);
        bool activeWarmup =
            ParseNonNegativeEnv("PERF_MULTI_ACTIVE_WARMUP", 0) == 1;
        int warmupDrainMs = ParseNonNegativeEnv("PERF_MULTI_WARMUP_DRAIN_MS",
            Math.Max(drainMs, 1000));
        int sndTimeoutMs = ParsePositiveEnv("PERF_MULTI_SNDTIMEO_MS", 5000);
        int rcvTimeoutMs = ParsePositiveEnv("PERF_MULTI_RCVTIMEO_MS", 5000);
        int readyTimeoutMs =
            ParsePositiveEnv("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);
        bool streamPattern = IsMultiStreamPattern(pattern);

        int defaultClients = 1000;
        int clientCount = Math.Max(1,
            ParsePositiveEnv("PERF_MULTI_CLIENTS", defaultClients));

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();

        try
        {
            if (streamPattern)
            {
                var streamClients = new List<RawTransportStreamClient>(clientCount);
                try
                {
                    for (int i = 0; i < clientCount; i++)
                    {
                        var client = new RawTransportStreamClient(endpoint,
                            sndTimeoutMs, rcvTimeoutMs);
                        client.Connect();
                        streamClients.Add(client);
                    }

                    if (streamClients.Count == 0)
                    {
                        Console.Error.WriteLine(
                            "multi_client_error:no_ready_connections");
                        return 2;
                    }

                    var payloadBody = new byte[size];
                    Array.Fill(payloadBody, (byte)'a');
                    int index = 0;

                    if (activeWarmup)
                    {
                        var warmupDeadline = DateTime.UtcNow.AddSeconds(
                            Math.Max(0, warmupSeconds));
                        while (DateTime.UtcNow < warmupDeadline)
                        {
                            RawTransportStreamClient client = streamClients[index];
                            client.SendFrame(payloadBody);
                            _ = client.ReceiveFramePayload();
                            index = (index + 1) % streamClients.Count;
                        }
                        if (warmupDrainMs > 0)
                            Thread.Sleep(warmupDrainMs);
                    }
                    else if (warmupSeconds > 0)
                    {
                        Thread.Sleep(warmupSeconds * 1000);
                    }

                    if (settleMs > 0)
                        Thread.Sleep(settleMs);

                    long measureCount = 0;
                    var sw = Stopwatch.StartNew();
                    var benchDeadline = DateTime.UtcNow.AddSeconds(
                        Math.Max(1, durationSeconds));
                    while (DateTime.UtcNow < benchDeadline)
                    {
                        RawTransportStreamClient client = streamClients[index];
                        client.SendFrame(payloadBody);
                        _ = client.ReceiveFramePayload();
                        measureCount++;
                        index = (index + 1) % streamClients.Count;
                    }
                    sw.Stop();

                    if (drainMs > 0)
                        Thread.Sleep(drainMs);
                    if (sizeTransitionDrainMs > 0)
                        Thread.Sleep(sizeTransitionDrainMs);

                    try
                    {
                        streamClients[0].SendFrame(MultiStopToken);
                    }
                    catch
                    {
                    }

                    double throughput = sw.Elapsed.TotalSeconds > 0.0
                        ? measureCount / sw.Elapsed.TotalSeconds
                        : 0.0;
                    double latencyUs = (sw.Elapsed.TotalMilliseconds * 1000.0)
                        / Math.Max(1.0, measureCount * 2.0);

                    PrintResult(pattern, transport, size, throughput, latencyUs);

                    wall.Stop();
                    process.Refresh();
                    TimeSpan cpuEnd = process.TotalProcessorTime;
                    double cpuSec =
                        Math.Max(0.0, (cpuEnd - cpuStart).TotalSeconds);
                    double wallSec = Math.Max(1e-9, wall.Elapsed.TotalSeconds);
                    double ncpu = Math.Max(1, Environment.ProcessorCount);
                    double cpuPct = (cpuSec / (wallSec * ncpu)) * 100.0;
                    double memMb = process.WorkingSet64 / (1024.0 * 1024.0);

                    Console.WriteLine(
                        $"RESULT,current,{pattern},{transport},{size},client_cpu_pct,{cpuPct}");
                    Console.WriteLine(
                        $"RESULT,current,{pattern},{transport},{size},client_mem_mb,{memMb}");
                    return 0;
                }
                finally
                {
                    foreach (var client in streamClients)
                    {
                        try
                        {
                            client.Dispose();
                        }
                        catch
                        {
                        }
                    }
                }
            }

            using var ctx = new Context();
            Zlink.SocketType clientType = ResolveClientSocketType(pattern);
            var clients = new List<Zlink.Socket>(clientCount);
            var monitors = new List<MonitorSocket>(clientCount);
            try
            {
                for (int i = 0; i < clientCount; i++)
                {
                    var client = new Zlink.Socket(ctx, clientType);
                    ConfigureTlsClientIfNeeded(client, transport);
                    client.SetOption(SocketOption.SndTimeo, sndTimeoutMs);
                    client.SetOption(SocketOption.RcvTimeo, rcvTimeoutMs);
                    var monitor = client.MonitorOpen(
                        SocketEvent.ConnectionReady | SocketEvent.Connected);
                    client.Connect(endpoint);
                    clients.Add(client);
                    monitors.Add(monitor);
                }

                var activeClients = new List<Zlink.Socket>(clients.Count);
                for (int i = 0; i < monitors.Count; i++)
                {
                    if (WaitMonitorReady(monitors[i], readyTimeoutMs, true))
                        activeClients.Add(clients[i]);
                }

                if (activeClients.Count == 0)
                {
                    Console.Error.WriteLine(
                        "multi_client_error:no_ready_connections");
                    return 2;
                }

                var payload = new byte[size];
                Array.Fill(payload, (byte)'a');
                var recv =
                    new byte[Math.Max(256, Math.Max(size, MultiStopToken.Length))];
                int index = 0;
                bool echoMode = IsEchoPattern(pattern);

                if (clientType == Zlink.SocketType.Pub)
                    Thread.Sleep(300);

                if (activeWarmup)
                {
                    var warmupDeadline = DateTime.UtcNow.AddSeconds(
                        Math.Max(0, warmupSeconds));
                    while (DateTime.UtcNow < warmupDeadline)
                    {
                        Zlink.Socket client = activeClients[index];
                        SendRetry(client, payload.AsSpan(), SendFlags.None);
                        if (echoMode)
                        {
                            ReceiveRetry(client, recv.AsSpan(),
                                ReceiveFlags.None);
                        }
                        index = (index + 1) % activeClients.Count;
                    }
                    if (warmupDrainMs > 0)
                        Thread.Sleep(warmupDrainMs);
                }
                else if (warmupSeconds > 0)
                {
                    Thread.Sleep(warmupSeconds * 1000);
                }

                if (settleMs > 0)
                    Thread.Sleep(settleMs);

                long measureCount = 0;
                var sw = Stopwatch.StartNew();
                var benchDeadline =
                    DateTime.UtcNow.AddSeconds(Math.Max(1, durationSeconds));
                while (DateTime.UtcNow < benchDeadline)
                {
                    Zlink.Socket client = activeClients[index];
                    SendRetry(client, payload.AsSpan(), SendFlags.None);
                    if (echoMode)
                        ReceiveRetry(client, recv.AsSpan(), ReceiveFlags.None);
                    measureCount++;
                    index = (index + 1) % activeClients.Count;
                }
                sw.Stop();

                if (drainMs > 0)
                    Thread.Sleep(drainMs);
                if (sizeTransitionDrainMs > 0)
                    Thread.Sleep(sizeTransitionDrainMs);

                double throughput = sw.Elapsed.TotalSeconds > 0.0
                    ? measureCount / sw.Elapsed.TotalSeconds
                    : 0.0;
                bool dealerDealerRtt = pattern.Equals("MULTI_DEALER_DEALER",
                    StringComparison.OrdinalIgnoreCase);
                double latencyDivisor = (echoMode || dealerDealerRtt)
                    ? measureCount * 2.0
                    : measureCount;
                double latencyUs = (sw.Elapsed.TotalMilliseconds * 1000.0)
                    / Math.Max(1.0, latencyDivisor);

                try
                {
                    SendRetry(activeClients[0], MultiStopToken.AsSpan(),
                        SendFlags.None);
                }
                catch
                {
                }

                PrintResult(pattern, transport, size, throughput, latencyUs);

                wall.Stop();
                process.Refresh();
                TimeSpan cpuEnd = process.TotalProcessorTime;
                double cpuSec = Math.Max(0.0, (cpuEnd - cpuStart).TotalSeconds);
                double wallSec = Math.Max(1e-9, wall.Elapsed.TotalSeconds);
                double ncpu = Math.Max(1, Environment.ProcessorCount);
                double cpuPct = (cpuSec / (wallSec * ncpu)) * 100.0;
                double memMb = process.WorkingSet64 / (1024.0 * 1024.0);

                Console.WriteLine(
                    $"RESULT,current,{pattern},{transport},{size},client_cpu_pct,{cpuPct}");
                Console.WriteLine(
                    $"RESULT,current,{pattern},{transport},{size},client_mem_mb,{memMb}");
                return 0;
            }
            finally
            {
                foreach (MonitorSocket monitor in monitors)
                {
                    try
                    {
                        monitor.Dispose();
                    }
                    catch
                    {
                    }
                }

                foreach (Zlink.Socket client in clients)
                {
                    try
                    {
                        client.Dispose();
                    }
                    catch
                    {
                    }
                }
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"multi_client_error:{ex.Message}");
            return 2;
        }
    }

    private static void ConfigureTlsServerIfNeeded(Zlink.Socket socket,
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
                "TLS certificate files not found under core/tests/certs/gen");
        }

        socket.SetOption(SocketOption.TlsCert, certPath);
        socket.SetOption(SocketOption.TlsKey, keyPath);
    }

    private static void ConfigureTlsClientIfNeeded(Zlink.Socket socket,
        string transport)
    {
        if (!transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            && !transport.Equals("wss", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        if (!TryResolvePerfTlsPaths(out _, out _, out string caPath))
        {
            throw new InvalidOperationException(
                "TLS CA file not found under core/tests/certs/gen");
        }

        socket.SetOption(SocketOption.TlsCa, caPath);
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
                string certDir = Path.Combine(dir.FullName, "core", "tests",
                    "certs", "gen");
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

using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;

internal sealed class Config
{
    public string Scenario { get; set; } = "s0";
    public string Transport { get; set; } = "tcp";
    public string BindHost { get; set; } = "127.0.0.1";
    public int Port { get; set; } = 27310;
    public int Ccu { get; set; } = 10000;
    public int Size { get; set; } = 1024;
    public int Inflight { get; set; } = 30;
    public int WarmupSec { get; set; } = 3;
    public int MeasureSec { get; set; } = 10;
    public int DrainTimeoutSec { get; set; } = 10;
    public int ConnectConcurrency { get; set; } = 256;
    public int ConnectTimeoutSec { get; set; } = 10;
    public int Backlog { get; set; } = 32768;
    public int Sndbuf { get; set; } = 256 * 1024;
    public int Rcvbuf { get; set; } = 256 * 1024;
    public int IoThreads { get; set; } = 1;
    public int LatencySampleRate { get; set; } = 1;
    public string ScenarioIdOverride { get; set; } = "";
    public string MetricsCsv { get; set; } = "";
    public string SummaryJson { get; set; } = "";
}

internal sealed class ResultRow
{
    public string ScenarioId { get; set; } = "";
    public string Transport { get; set; } = "";
    public int Ccu { get; set; }
    public int Inflight { get; set; }
    public int Size { get; set; }
    public long ConnectSuccess { get; set; }
    public long ConnectFail { get; set; }
    public long ConnectTimeout { get; set; }
    public long Sent { get; set; }
    public long Recv { get; set; }
    public double IncompleteRatio { get; set; }
    public double Throughput { get; set; }
    public double P50 { get; set; }
    public double P95 { get; set; }
    public double P99 { get; set; }
    public Dictionary<int, long> ErrorsByErrno { get; set; } = new();
    public string PassFail { get; set; } = "FAIL";
    public long DrainTimeoutCount { get; set; }
    public long GatingViolation { get; set; }
}

internal sealed class ErrorBag
{
    private readonly ConcurrentDictionary<int, long> _byErrno = new();

    public void RecordErrno(int err)
    {
        if (err <= 0)
        {
            return;
        }

        _byErrno.AddOrUpdate(err, 1, (_, old) => old + 1);
    }

    public void RecordException(Exception ex)
    {
        if (TryExtractErrno(ex, out var err))
        {
            RecordErrno(err);
        }
    }

    public Dictionary<int, long> SnapshotSorted()
    {
        var sorted = new SortedDictionary<int, long>();
        foreach (var kv in _byErrno)
        {
            sorted[kv.Key] = kv.Value;
        }
        return new Dictionary<int, long>(sorted);
    }

    private static bool TryExtractErrno(Exception ex, out int err)
    {
        err = 0;

        if (ex is SocketException se)
        {
            err = se.ErrorCode;
            return true;
        }

        if (ex.InnerException is SocketException innerSe)
        {
            err = innerSe.ErrorCode;
            return true;
        }

        if (ex is IOException io && io.InnerException is SocketException ioSe)
        {
            err = ioSe.ErrorCode;
            return true;
        }

        return false;
    }
}

internal sealed class BenchmarkState
{
    private readonly Config _cfg;
    private readonly object _latLock = new();
    private readonly List<double> _latenciesUs = new();

    private volatile bool _sendEnabled;
    private volatile bool _measureEnabled;
    private long _pendingTotal;
    private long _sentMeasure;
    private long _recvMeasure;
    private long _gatingViolation;

    public BenchmarkState(Config cfg)
    {
        _cfg = cfg;
    }

    public void SetPhase(bool sendEnabled, bool measureEnabled)
    {
        _sendEnabled = sendEnabled;
        _measureEnabled = measureEnabled;
    }

    public bool SendEnabled => _sendEnabled;

    public int CurrentPhase => _measureEnabled ? 1 : 0;

    public void ResetMeasureMetrics()
    {
        Interlocked.Exchange(ref _sentMeasure, 0);
        Interlocked.Exchange(ref _recvMeasure, 0);
        Interlocked.Exchange(ref _gatingViolation, 0);

        lock (_latLock)
        {
            _latenciesUs.Clear();
        }
    }

    public void OnSent(int phase)
    {
        if (phase == 1)
        {
            Interlocked.Increment(ref _sentMeasure);
        }

        Interlocked.Increment(ref _pendingTotal);
    }

    public void OnRecv(int phase, ulong sentNs)
    {
        var before = Interlocked.Decrement(ref _pendingTotal) + 1;
        if (before <= 0)
        {
            Interlocked.Increment(ref _gatingViolation);
        }

        if (phase != 1)
        {
            return;
        }

        var recvIdx = Interlocked.Increment(ref _recvMeasure);
        if (!_measureEnabled)
        {
            return;
        }

        var sampleRate = Math.Max(1, _cfg.LatencySampleRate);
        if (sampleRate > 1 && (recvIdx % sampleRate) != 0)
        {
            return;
        }

        var now = TimeUtil.NowNs();
        var delta = now >= sentNs ? now - sentNs : 0;
        lock (_latLock)
        {
            _latenciesUs.Add(delta / 1000.0);
        }
    }

    public void DropPending(int n)
    {
        if (n <= 0)
        {
            return;
        }

        Interlocked.Add(ref _pendingTotal, -n);
    }

    public long PendingTotal => Interlocked.Read(ref _pendingTotal);
    public long SentMeasure => Interlocked.Read(ref _sentMeasure);
    public long RecvMeasure => Interlocked.Read(ref _recvMeasure);
    public long GatingViolation => Interlocked.Read(ref _gatingViolation);

    public List<double> LatencySnapshot()
    {
        lock (_latLock)
        {
            return new List<double>(_latenciesUs);
        }
    }
}

internal sealed class EchoServer : IAsyncDisposable
{
    private readonly Config _cfg;
    private readonly bool _echoEnabled;
    private readonly ErrorBag _errors;
    private readonly CancellationTokenSource _cts = new();
    private readonly ConcurrentDictionary<long, Socket> _clients = new();
    private readonly ConcurrentDictionary<long, Task> _clientTasks = new();

    private Socket? _listener;
    private Task? _acceptTask;
    private long _nextClientId;
    private long _connectEvents;
    private long _disconnectEvents;

    public EchoServer(Config cfg, bool echoEnabled, ErrorBag errors)
    {
        _cfg = cfg;
        _echoEnabled = echoEnabled;
        _errors = errors;
    }

    public long ConnectEvents => Interlocked.Read(ref _connectEvents);
    public long DisconnectEvents => Interlocked.Read(ref _disconnectEvents);

    public bool Start()
    {
        try
        {
            var addr = IPAddress.Parse(_cfg.BindHost);
            var endpoint = new IPEndPoint(addr, _cfg.Port);

            _listener = new Socket(endpoint.AddressFamily, SocketType.Stream, ProtocolType.Tcp);
            _listener.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
            _listener.ReceiveBufferSize = _cfg.Rcvbuf;
            _listener.SendBufferSize = _cfg.Sndbuf;
            _listener.Bind(endpoint);
            _listener.Listen(Math.Max(1, _cfg.Backlog));

            _acceptTask = AcceptLoopAsync(_cts.Token);
            return true;
        }
        catch (Exception ex)
        {
            _errors.RecordException(ex);
            return false;
        }
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync().ConfigureAwait(false);
        _cts.Dispose();
    }

    public async Task StopAsync()
    {
        _cts.Cancel();
        if (_listener is not null)
        {
            SafeClose(_listener);
            _listener = null;
        }

        foreach (var kv in _clients)
        {
            SafeClose(kv.Value);
        }

        var acceptTask = _acceptTask;
        if (acceptTask is not null)
        {
            try
            {
                await acceptTask.ConfigureAwait(false);
            }
            catch
            {
                // no-op
            }
        }

        if (_clientTasks.Count > 0)
        {
            try
            {
                await Task.WhenAll(_clientTasks.Values).ConfigureAwait(false);
            }
            catch
            {
                // no-op
            }
        }

        _clients.Clear();
        _clientTasks.Clear();
    }

    private async Task AcceptLoopAsync(CancellationToken token)
    {
        if (_listener is null)
        {
            return;
        }

        while (!token.IsCancellationRequested)
        {
            Socket? client = null;
            try
            {
                client = await _listener.AcceptAsync(token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            catch (Exception ex)
            {
                _errors.RecordException(ex);
                continue;
            }

            try
            {
                ConfigureConnectedSocket(client, _cfg);

                var clientId = Interlocked.Increment(ref _nextClientId);
                _clients[clientId] = client;
                Interlocked.Increment(ref _connectEvents);

                var task = RunClientSessionAsync(clientId, client, token);
                _clientTasks[clientId] = task;
            }
            catch (Exception ex)
            {
                _errors.RecordException(ex);
                SafeClose(client);
            }
        }
    }

    private async Task RunClientSessionAsync(long clientId, Socket socket, CancellationToken token)
    {
        byte[] header = new byte[4];
        byte[] body = Array.Empty<byte>();

        try
        {
            while (!token.IsCancellationRequested)
            {
                var headerRead = await ReceiveExactAsync(socket, header.AsMemory(0, 4), token).ConfigureAwait(false);
                if (headerRead == 0)
                {
                    break;
                }

                if (headerRead < 4)
                {
                    throw new IOException("short header");
                }

                var len = ReadU32Be(header.AsSpan(0, 4));
                if (len <= 0 || len > 4 * 1024 * 1024)
                {
                    throw new IOException($"invalid frame length: {len}");
                }

                if (body.Length < len)
                {
                    body = new byte[len];
                }

                var bodyRead = await ReceiveExactAsync(socket, body.AsMemory(0, len), token).ConfigureAwait(false);
                if (bodyRead < len)
                {
                    break;
                }

                if (!_echoEnabled)
                {
                    continue;
                }

                await SendExactAsync(socket, header.AsMemory(0, 4), token).ConfigureAwait(false);
                await SendExactAsync(socket, body.AsMemory(0, len), token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException)
        {
            // normal on shutdown
        }
        catch (ObjectDisposedException)
        {
            // normal on shutdown
        }
        catch (Exception ex)
        {
            _errors.RecordException(ex);
        }
        finally
        {
            _clients.TryRemove(clientId, out _);
            _clientTasks.TryRemove(clientId, out _);
            Interlocked.Increment(ref _disconnectEvents);
            SafeClose(socket);
        }
    }

    private static int ReadU32Be(ReadOnlySpan<byte> src)
    {
        return unchecked((int)BinaryPrimitives.ReadUInt32BigEndian(src));
    }

    private static async ValueTask<int> ReceiveExactAsync(Socket socket, Memory<byte> buffer, CancellationToken token)
    {
        var read = 0;
        while (read < buffer.Length)
        {
            var n = await socket.ReceiveAsync(buffer[read..], SocketFlags.None, token).ConfigureAwait(false);
            if (n == 0)
            {
                return read;
            }

            read += n;
        }

        return read;
    }

    private static async ValueTask SendExactAsync(Socket socket, ReadOnlyMemory<byte> buffer, CancellationToken token)
    {
        var sent = 0;
        while (sent < buffer.Length)
        {
            var n = await socket.SendAsync(buffer[sent..], SocketFlags.None, token).ConfigureAwait(false);
            if (n <= 0)
            {
                throw new IOException("socket send returned 0");
            }

            sent += n;
        }
    }

    private static void ConfigureConnectedSocket(Socket socket, Config cfg)
    {
        socket.NoDelay = true;
        socket.SendBufferSize = cfg.Sndbuf;
        socket.ReceiveBufferSize = cfg.Rcvbuf;
        socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
    }

    private static void SafeClose(Socket socket)
    {
        try
        {
            socket.Shutdown(SocketShutdown.Both);
        }
        catch
        {
            // no-op
        }

        try
        {
            socket.Close();
        }
        catch
        {
            // no-op
        }

        try
        {
            socket.Dispose();
        }
        catch
        {
            // no-op
        }
    }
}

internal sealed class ConnectResult
{
    public required List<Socket> Clients { get; init; }
    public required long Success { get; init; }
    public required long Fail { get; init; }
    public required long Timeout { get; init; }
}

internal static class TimeUtil
{
    private static readonly double NsPerTick = 1_000_000_000.0 / Stopwatch.Frequency;

    public static ulong NowNs()
    {
        return (ulong)(Stopwatch.GetTimestamp() * NsPerTick);
    }
}

internal static class Program
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    public static async Task<int> Main(string[] args)
    {
        var cfg = ParseConfig(args);

        if (args.Contains("--help") || (cfg.Scenario is not ("s0" or "s1" or "s2")))
        {
            PrintUsage();
            return cfg.Scenario is "s0" or "s1" or "s2" ? 0 : 2;
        }

        cfg.Ccu = Math.Max(1, cfg.Ccu);
        cfg.Size = Math.Max(16, cfg.Size);
        cfg.Inflight = Math.Max(1, cfg.Inflight);
        cfg.ConnectConcurrency = Math.Max(1, cfg.ConnectConcurrency);
        cfg.ConnectTimeoutSec = Math.Max(1, cfg.ConnectTimeoutSec);
        cfg.IoThreads = Math.Max(1, cfg.IoThreads);
        cfg.LatencySampleRate = Math.Max(1, cfg.LatencySampleRate);

        TuneThreadPool(cfg);

        var row = new ResultRow();
        FillCommonRow(row, cfg);

        if (cfg.Transport != "tcp")
        {
            row.PassFail = "SKIP";
            PrintRow(row);
            AppendCsv(cfg.MetricsCsv, row);
            WriteSummaryJson(cfg.SummaryJson, row);
            Console.Error.WriteLine($"[stream-dotnet] transport '{cfg.Transport}' skipped (tcp only)");
            return 0;
        }

        var ok = false;
        if (cfg.Scenario == "s0")
        {
            ok = await RunS0Async(cfg, row).ConfigureAwait(false);
        }
        else if (cfg.Scenario == "s1")
        {
            ok = await RunS1OrS2Async(cfg, row, withSend: false).ConfigureAwait(false);
        }
        else
        {
            ok = await RunS1OrS2Async(cfg, row, withSend: true).ConfigureAwait(false);
        }

        PrintRow(row);
        AppendCsv(cfg.MetricsCsv, row);
        WriteSummaryJson(cfg.SummaryJson, row);
        return ok ? 0 : 2;
    }

    private static async Task<bool> RunS0Async(Config cfg, ResultRow row)
    {
        FillCommonRow(row, cfg);

        var errors = new ErrorBag();
        await using var server = new EchoServer(cfg, echoEnabled: true, errors);
        if (!server.Start())
        {
            row.PassFail = "FAIL";
            row.ErrorsByErrno = errors.SnapshotSorted();
            return false;
        }

        var ok = false;
        Socket? client = null;
        try
        {
            var endpoint = new IPEndPoint(IPAddress.Parse(cfg.BindHost), cfg.Port);
            client = new Socket(endpoint.AddressFamily, SocketType.Stream, ProtocolType.Tcp);
            ConfigureConnectedSocket(client, cfg);

            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(Math.Max(1, cfg.ConnectTimeoutSec)));
            await client.ConnectAsync(endpoint, cts.Token).ConfigureAwait(false);

            var bodySize = Math.Max(9, cfg.Size);
            var msg = new byte[4 + bodySize];
            WriteU32Be(msg.AsSpan(0, 4), (uint)bodySize);
            msg[4] = 1;
            WriteU64Le(msg.AsSpan(5, 8), TimeUtil.NowNs());
            msg.AsSpan(13).Fill(0x44);

            await SendExactAsync(client, msg, cts.Token).ConfigureAwait(false);

            var hdr = new byte[4];
            var hdrRead = await ReceiveExactAsync(client, hdr.AsMemory(0, 4), cts.Token).ConfigureAwait(false);
            if (hdrRead == 4)
            {
                var len = ReadU32Be(hdr.AsSpan(0, 4));
                var body = new byte[len];
                var bodyRead = await ReceiveExactAsync(client, body.AsMemory(0, len), cts.Token).ConfigureAwait(false);
                ok = bodyRead == len && len == bodySize && body.Length >= 1 && body[0] == 1;
            }
        }
        catch (Exception ex)
        {
            errors.RecordException(ex);
            ok = false;
        }
        finally
        {
            if (client is not null)
            {
                SafeClose(client);
            }
        }

        row.ConnectSuccess = ok ? 1 : 0;
        row.ConnectFail = ok ? 0 : 1;
        row.Sent = ok ? 1 : 0;
        row.Recv = ok ? 1 : 0;
        row.PassFail = ok ? "PASS" : "FAIL";
        row.ErrorsByErrno = errors.SnapshotSorted();
        return ok;
    }

    private static async Task<bool> RunS1OrS2Async(Config cfg, ResultRow row, bool withSend)
    {
        FillCommonRow(row, cfg);

        var errors = new ErrorBag();
        var state = new BenchmarkState(cfg);

        await using var server = new EchoServer(cfg, withSend, errors);
        if (!server.Start())
        {
            row.PassFail = "FAIL";
            row.ErrorsByErrno = errors.SnapshotSorted();
            return false;
        }

        var connectResult = await ConnectClientsAsync(cfg, errors).ConfigureAwait(false);
        var clients = connectResult.Clients;
        row.ConnectSuccess = connectResult.Success;
        row.ConnectFail = connectResult.Fail;
        row.ConnectTimeout = connectResult.Timeout;

        var serverDeadline = DateTime.UtcNow.AddSeconds(5);
        while (DateTime.UtcNow < serverDeadline)
        {
            if (server.ConnectEvents >= row.ConnectSuccess)
            {
                break;
            }

            await Task.Delay(10).ConfigureAwait(false);
        }

        var ok = row.ConnectSuccess == cfg.Ccu && row.ConnectFail == 0 && row.ConnectTimeout == 0;
        if (!withSend || !ok)
        {
            foreach (var client in clients)
            {
                SafeClose(client);
            }

            row.PassFail = ok ? "PASS" : "FAIL";
            row.ErrorsByErrno = errors.SnapshotSorted();
            return ok;
        }

        var runCts = new CancellationTokenSource();
        var runToken = runCts.Token;
        var startGate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var trafficTasks = new List<Task>(clients.Count);
        foreach (var client in clients)
        {
            trafficTasks.Add(RunClientTrafficAsync(client, cfg, state, errors, startGate.Task, runToken));
        }

        state.SetPhase(sendEnabled: true, measureEnabled: false);
        startGate.TrySetResult();

        if (cfg.WarmupSec > 0)
        {
            await Task.Delay(TimeSpan.FromSeconds(cfg.WarmupSec)).ConfigureAwait(false);
        }

        state.ResetMeasureMetrics();
        state.SetPhase(sendEnabled: true, measureEnabled: true);
        await Task.Delay(TimeSpan.FromSeconds(Math.Max(1, cfg.MeasureSec))).ConfigureAwait(false);
        state.SetPhase(sendEnabled: false, measureEnabled: false);

        var drainDeadline = DateTime.UtcNow.AddSeconds(Math.Max(1, cfg.DrainTimeoutSec));
        while (state.PendingTotal > 0 && DateTime.UtcNow < drainDeadline)
        {
            await Task.Delay(5).ConfigureAwait(false);
        }

        if (state.PendingTotal > 0)
        {
            row.DrainTimeoutCount = 1;
        }

        row.Sent = state.SentMeasure;
        row.Recv = state.RecvMeasure;
        row.IncompleteRatio = row.Sent > 0 ? (double)(row.Sent - row.Recv) / row.Sent : 0.0;
        row.Throughput = cfg.MeasureSec > 0 ? (double)row.Recv / cfg.MeasureSec : 0.0;
        row.GatingViolation = state.GatingViolation;

        var lats = state.LatencySnapshot();
        row.P50 = Percentile(lats, 0.50);
        row.P95 = Percentile(lats, 0.95);
        row.P99 = Percentile(lats, 0.99);

        ok = ok
             && row.Recv > 0
             && row.IncompleteRatio <= 0.01
             && row.DrainTimeoutCount == 0
             && row.GatingViolation == 0;

        runCts.Cancel();
        foreach (var client in clients)
        {
            SafeClose(client);
        }

        try
        {
            await Task.WhenAll(trafficTasks).ConfigureAwait(false);
        }
        catch
        {
            // no-op
        }

        row.PassFail = ok ? "PASS" : "FAIL";
        row.ErrorsByErrno = errors.SnapshotSorted();
        return ok;
    }

    private static async Task RunClientTrafficAsync(
        Socket socket,
        Config cfg,
        BenchmarkState state,
        ErrorBag errors,
        Task startGate,
        CancellationToken token)
    {
        var pending = 0;
        var bodySize = Math.Max(9, cfg.Size);
        var sendPacket = new byte[4 + bodySize];
        WriteU32Be(sendPacket.AsSpan(0, 4), (uint)bodySize);
        sendPacket.AsSpan(13).Fill(0x33);

        var recvHeader = new byte[4];
        var recvBody = new byte[Math.Max(bodySize, 64)];

        try
        {
            await startGate.ConfigureAwait(false);

            while (!token.IsCancellationRequested)
            {
                while (pending < cfg.Inflight && state.SendEnabled)
                {
                    var phase = state.CurrentPhase;
                    sendPacket[4] = (byte)phase;
                    WriteU64Le(sendPacket.AsSpan(5, 8), TimeUtil.NowNs());

                    await SendExactAsync(socket, sendPacket, token).ConfigureAwait(false);
                    state.OnSent(phase);
                    pending += 1;
                }

                if (pending == 0 && !state.SendEnabled)
                {
                    break;
                }

                var headerRead = await ReceiveExactAsync(socket, recvHeader.AsMemory(0, 4), token).ConfigureAwait(false);
                if (headerRead == 0)
                {
                    break;
                }

                if (headerRead < 4)
                {
                    throw new IOException("short header");
                }

                var len = ReadU32Be(recvHeader.AsSpan(0, 4));
                if (len <= 0 || len > 4 * 1024 * 1024)
                {
                    throw new IOException($"invalid frame length: {len}");
                }

                if (recvBody.Length < len)
                {
                    recvBody = new byte[len];
                }

                var bodyRead = await ReceiveExactAsync(socket, recvBody.AsMemory(0, len), token).ConfigureAwait(false);
                if (bodyRead < len)
                {
                    break;
                }

                if (pending > 0)
                {
                    pending -= 1;
                }
                else
                {
                    state.DropPending(0);
                }

                var phaseRecv = 0;
                ulong sentNs = 0;
                if (len >= 9)
                {
                    phaseRecv = recvBody[0];
                    sentNs = ReadU64Le(recvBody.AsSpan(1, 8));
                }

                state.OnRecv(phaseRecv, sentNs);
            }
        }
        catch (OperationCanceledException)
        {
            // normal on shutdown
        }
        catch (ObjectDisposedException)
        {
            // normal on shutdown
        }
        catch (Exception ex)
        {
            errors.RecordException(ex);
        }
        finally
        {
            state.DropPending(pending);
            SafeClose(socket);
        }
    }

    private static async Task<ConnectResult> ConnectClientsAsync(Config cfg, ErrorBag errors)
    {
        var endpoint = new IPEndPoint(IPAddress.Parse(cfg.BindHost), cfg.Port);
        var success = 0L;
        var fail = 0L;
        var timeout = 0L;
        var sockets = new ConcurrentBag<Socket>();

        using var sem = new SemaphoreSlim(Math.Max(1, Math.Min(cfg.ConnectConcurrency, cfg.Ccu)));
        var tasks = new List<Task>(cfg.Ccu);

        for (var i = 0; i < cfg.Ccu; i++)
        {
            await sem.WaitAsync().ConfigureAwait(false);
            tasks.Add(Task.Run(async () =>
            {
                Socket? socket = null;
                try
                {
                    socket = new Socket(endpoint.AddressFamily, SocketType.Stream, ProtocolType.Tcp);
                    ConfigureConnectedSocket(socket, cfg);

                    using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(Math.Max(1, cfg.ConnectTimeoutSec)));
                    await socket.ConnectAsync(endpoint, cts.Token).ConfigureAwait(false);

                    sockets.Add(socket);
                    Interlocked.Increment(ref success);
                    socket = null;
                }
                catch (OperationCanceledException)
                {
                    Interlocked.Increment(ref timeout);
                }
                catch (SocketException se) when (se.SocketErrorCode == SocketError.TimedOut)
                {
                    Interlocked.Increment(ref timeout);
                    errors.RecordException(se);
                }
                catch (Exception ex)
                {
                    Interlocked.Increment(ref fail);
                    errors.RecordException(ex);
                }
                finally
                {
                    if (socket is not null)
                    {
                        SafeClose(socket);
                    }

                    sem.Release();
                }
            }));
        }

        await Task.WhenAll(tasks).ConfigureAwait(false);
        return new ConnectResult
        {
            Clients = sockets.ToList(),
            Success = success,
            Fail = fail,
            Timeout = timeout
        };
    }

    private static void FillCommonRow(ResultRow row, Config cfg)
    {
        row.ScenarioId = string.IsNullOrWhiteSpace(cfg.ScenarioIdOverride) ? cfg.Scenario : cfg.ScenarioIdOverride;
        row.Transport = cfg.Transport;
        row.Ccu = cfg.Ccu;
        row.Inflight = cfg.Inflight;
        row.Size = cfg.Size;
    }

    private static void PrintRow(ResultRow row)
    {
        Console.WriteLine(
            "RESULT scenario={0} transport={1} ccu={2} size={3} inflight={4} connect_success={5} connect_fail={6} connect_timeout={7} sent={8} recv={9} incomplete_ratio={10:F6} throughput={11:F2} p50_us={12:F2} p95_us={13:F2} p99_us={14:F2} drain_timeout={15} gating_violation={16} pass_fail={17}",
            row.ScenarioId,
            row.Transport,
            row.Ccu,
            row.Size,
            row.Inflight,
            row.ConnectSuccess,
            row.ConnectFail,
            row.ConnectTimeout,
            row.Sent,
            row.Recv,
            row.IncompleteRatio,
            row.Throughput,
            row.P50,
            row.P95,
            row.P99,
            row.DrainTimeoutCount,
            row.GatingViolation,
            row.PassFail);

        Console.WriteLine(
            "METRIC scenario_id={0} transport={1} ccu={2} inflight={3} size={4} connect_success={5} connect_fail={6} connect_timeout={7} sent={8} recv={9} incomplete_ratio={10:F6} throughput={11:F2} p50={12:F2} p95={13:F2} p99={14:F2} errors_by_errno={15} pass_fail={16}",
            row.ScenarioId,
            row.Transport,
            row.Ccu,
            row.Inflight,
            row.Size,
            row.ConnectSuccess,
            row.ConnectFail,
            row.ConnectTimeout,
            row.Sent,
            row.Recv,
            row.IncompleteRatio,
            row.Throughput,
            row.P50,
            row.P95,
            row.P99,
            ErrorsToString(row.ErrorsByErrno),
            row.PassFail);
    }

    private static void AppendCsv(string path, ResultRow row)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        var dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(dir))
        {
            Directory.CreateDirectory(dir);
        }

        var exists = File.Exists(path);
        using var fs = new FileStream(path, exists ? FileMode.Append : FileMode.Create, FileAccess.Write, FileShare.Read);
        using var sw = new StreamWriter(fs);

        if (!exists)
        {
            sw.WriteLine("scenario_id,transport,ccu,inflight,size,connect_success,connect_fail,connect_timeout,sent,recv,incomplete_ratio,throughput,p50,p95,p99,errors_by_errno,pass_fail");
        }

        sw.WriteLine(
            "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10:F6},{11:F2},{12:F2},{13:F2},{14:F2},\"{15}\",{16}",
            row.ScenarioId,
            row.Transport,
            row.Ccu,
            row.Inflight,
            row.Size,
            row.ConnectSuccess,
            row.ConnectFail,
            row.ConnectTimeout,
            row.Sent,
            row.Recv,
            row.IncompleteRatio,
            row.Throughput,
            row.P50,
            row.P95,
            row.P99,
            ErrorsToString(row.ErrorsByErrno),
            row.PassFail);
    }

    private static void WriteSummaryJson(string path, ResultRow row)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        var dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(dir))
        {
            Directory.CreateDirectory(dir);
        }

        var summary = new
        {
            scenario_id = row.ScenarioId,
            transport = row.Transport,
            ccu = row.Ccu,
            inflight = row.Inflight,
            size = row.Size,
            connect_success = row.ConnectSuccess,
            connect_fail = row.ConnectFail,
            connect_timeout = row.ConnectTimeout,
            sent = row.Sent,
            recv = row.Recv,
            incomplete_ratio = row.IncompleteRatio,
            throughput = row.Throughput,
            p50 = row.P50,
            p95 = row.P95,
            p99 = row.P99,
            errors_by_errno = ErrorsToString(row.ErrorsByErrno),
            pass_fail = row.PassFail
        };

        File.WriteAllText(path, JsonSerializer.Serialize(summary, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static double Percentile(List<double> samples, double p)
    {
        if (samples.Count == 0)
        {
            return 0.0;
        }

        samples.Sort();
        var idx = (int)Math.Max(0.0, Math.Ceiling(samples.Count * p) - 1.0);
        idx = Math.Min(idx, samples.Count - 1);
        return samples[idx];
    }

    private static string ErrorsToString(Dictionary<int, long> errors)
    {
        if (errors.Count == 0)
        {
            return "";
        }

        var sorted = errors.OrderBy(kv => kv.Key);
        return string.Join(";", sorted.Select(kv => $"{kv.Key}:{kv.Value}"));
    }

    private static void PrintUsage()
    {
        Console.WriteLine("Usage: StreamSocketScenario --scenario s0|s1|s2 [options]");
        Console.WriteLine("Options:");
        Console.WriteLine("  --transport tcp                  (dotnet runner supports tcp only)");
        Console.WriteLine("  --port N                         (default 27310)");
        Console.WriteLine("  --ccu N                          (default 10000)");
        Console.WriteLine("  --size N                         (default 1024)");
        Console.WriteLine("  --inflight N                     (per-connection, default 30)");
        Console.WriteLine("  --warmup N                       (default 3 sec)");
        Console.WriteLine("  --measure N                      (default 10 sec)");
        Console.WriteLine("  --drain-timeout N                (default 10 sec)");
        Console.WriteLine("  --connect-concurrency N          (default 256)");
        Console.WriteLine("  --connect-timeout N              (default 10 sec)");
        Console.WriteLine("  --backlog N                      (default 32768)");
        Console.WriteLine("  --sndbuf N                       (default 262144)");
        Console.WriteLine("  --rcvbuf N                       (default 262144)");
        Console.WriteLine("  --io-threads N                   (default 1)");
        Console.WriteLine("  --latency-sample-rate N          (default 1, 1=all samples)");
        Console.WriteLine("  --scenario-id ID                 override scenario_id output");
        Console.WriteLine("  --metrics-csv PATH               append row to csv");
        Console.WriteLine("  --summary-json PATH              write row json");
    }

    private static Config ParseConfig(string[] args)
    {
        var cfg = new Config
        {
            Scenario = ArgStr(args, "--scenario", "s0"),
            Transport = ArgStr(args, "--transport", "tcp"),
            BindHost = ArgStr(args, "--bind-host", "127.0.0.1"),
            Port = ArgInt(args, "--port", 27310),
            Ccu = ArgInt(args, "--ccu", 10000),
            Size = ArgInt(args, "--size", 1024),
            Inflight = ArgInt(args, "--inflight", 30),
            WarmupSec = ArgInt(args, "--warmup", 3),
            MeasureSec = ArgInt(args, "--measure", 10),
            DrainTimeoutSec = ArgInt(args, "--drain-timeout", 10),
            ConnectConcurrency = ArgInt(args, "--connect-concurrency", 256),
            ConnectTimeoutSec = ArgInt(args, "--connect-timeout", 10),
            Backlog = ArgInt(args, "--backlog", 32768),
            Sndbuf = ArgInt(args, "--sndbuf", 256 * 1024),
            Rcvbuf = ArgInt(args, "--rcvbuf", 256 * 1024),
            IoThreads = ArgInt(args, "--io-threads", 1),
            LatencySampleRate = ArgInt(args, "--latency-sample-rate", 1),
            ScenarioIdOverride = ArgStr(args, "--scenario-id", ""),
            MetricsCsv = ArgStr(args, "--metrics-csv", ""),
            SummaryJson = ArgStr(args, "--summary-json", "")
        };

        return cfg;
    }

    private static string ArgStr(string[] args, string key, string fallback)
    {
        for (var i = 0; i + 1 < args.Length; i++)
        {
            if (args[i] == key)
            {
                return args[i + 1];
            }
        }

        return fallback;
    }

    private static int ArgInt(string[] args, string key, int fallback)
    {
        var value = fallback;
        for (var i = 0; i + 1 < args.Length; i++)
        {
            if (args[i] == key && int.TryParse(args[i + 1], NumberStyles.Integer, Inv, out var parsed))
            {
                value = parsed;
            }
        }

        return value;
    }

    private static void TuneThreadPool(Config cfg)
    {
        ThreadPool.GetMinThreads(out var worker, out var io);
        var desiredWorker = Math.Max(worker, Math.Max(cfg.ConnectConcurrency, cfg.IoThreads * 128));
        ThreadPool.SetMinThreads(desiredWorker, io);
    }

    private static void ConfigureConnectedSocket(Socket socket, Config cfg)
    {
        socket.NoDelay = true;
        socket.SendBufferSize = cfg.Sndbuf;
        socket.ReceiveBufferSize = cfg.Rcvbuf;
        socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
    }

    private static int ReadU32Be(ReadOnlySpan<byte> src)
    {
        return unchecked((int)BinaryPrimitives.ReadUInt32BigEndian(src));
    }

    private static ulong ReadU64Le(ReadOnlySpan<byte> src)
    {
        return BinaryPrimitives.ReadUInt64LittleEndian(src);
    }

    private static void WriteU32Be(Span<byte> dst, uint value)
    {
        BinaryPrimitives.WriteUInt32BigEndian(dst, value);
    }

    private static void WriteU64Le(Span<byte> dst, ulong value)
    {
        BinaryPrimitives.WriteUInt64LittleEndian(dst, value);
    }

    private static async ValueTask<int> ReceiveExactAsync(Socket socket, Memory<byte> buffer, CancellationToken token)
    {
        var read = 0;
        while (read < buffer.Length)
        {
            var n = await socket.ReceiveAsync(buffer[read..], SocketFlags.None, token).ConfigureAwait(false);
            if (n == 0)
            {
                return read;
            }

            read += n;
        }

        return read;
    }

    private static async ValueTask SendExactAsync(Socket socket, ReadOnlyMemory<byte> buffer, CancellationToken token)
    {
        var sent = 0;
        while (sent < buffer.Length)
        {
            var n = await socket.SendAsync(buffer[sent..], SocketFlags.None, token).ConfigureAwait(false);
            if (n <= 0)
            {
                throw new IOException("socket send returned 0");
            }

            sent += n;
        }
    }

    private static void SafeClose(Socket socket)
    {
        try
        {
            socket.Shutdown(SocketShutdown.Both);
        }
        catch
        {
            // no-op
        }

        try
        {
            socket.Close();
        }
        catch
        {
            // no-op
        }

        try
        {
            socket.Dispose();
        }
        catch
        {
            // no-op
        }
    }
}

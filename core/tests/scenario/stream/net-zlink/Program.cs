using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.Json;
using Zlink;

internal sealed class Config
{
    public string Scenario { get; set; } = "s0";
    public string Role { get; set; } = "both";
    public string Transport { get; set; } = "tcp";
    public string BindHost { get; set; } = "127.0.0.1";
    public int Port { get; set; } = 27110;
    public int Ccu { get; set; } = 10000;
    public int Size { get; set; } = 1024;
    public int Inflight { get; set; } = 30;
    public int WarmupSec { get; set; } = 3;
    public int MeasureSec { get; set; } = 10;
    public int DrainTimeoutSec { get; set; } = 10;
    public int ConnectConcurrency { get; set; } = 256;
    public int ConnectTimeoutSec { get; set; } = 10;
    public int ConnectRetries { get; set; } = 3;
    public int ConnectRetryDelayMs { get; set; } = 100;
    public int Backlog { get; set; } = 32768;
    public int Hwm { get; set; } = 1000000;
    public int Sndbuf { get; set; } = 256 * 1024;
    public int Rcvbuf { get; set; } = 256 * 1024;
    public int IoThreads { get; set; } = 1;
    public int Shards { get; set; } = 1;
    public int SendBatch { get; set; } = 1;
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
        if (ex is ZlinkException ze)
        {
            RecordErrno(ze.Errno);
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
        var sampleRate = _cfg.LatencySampleRate;
        if (sampleRate == 0)
        {
            return;
        }

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

internal sealed class ClientConn
{
    public required Socket Socket { get; init; }
    public required byte[] PeerRoutingId { get; init; }
}

internal sealed class TrafficConnState
{
    public TrafficConnState(ClientConn conn, int payloadSize, int inflight)
    {
        Conn = conn;
        InflightLimit = Math.Max(1, inflight);
        PacketBodySize = Math.Max(9, payloadSize);
        PacketSize = 4 + PacketBodySize;
        MaxBatchPackets = Math.Max(1, Math.Min(InflightLimit, 32));
        SendPayload = new byte[PacketSize * MaxBatchPackets];
        RecvPartial = new byte[PacketSize];
        for (var i = 0; i < MaxBatchPackets; i++)
        {
            var offset = i * PacketSize;
            BinaryPrimitives.WriteUInt32BigEndian(SendPayload.AsSpan(offset, 4), (uint)PacketBodySize);
            SendPayload.AsSpan(offset + 13, PacketSize - 13).Fill(0x33);
        }
    }

    public ClientConn Conn { get; }
    public int Pending { get; set; }
    public int PendingSendBytes { get; set; }
    public int PendingSendPacketCount { get; set; }
    public int PendingSendPhase { get; set; }
    public bool PendingSendRoutingSent { get; set; }
    public bool RecvPayloadPending { get; set; }
    public int RecvPartialLen { get; set; }
    public int InflightLimit { get; }
    public int PacketBodySize { get; }
    public int PacketSize { get; }
    public int MaxBatchPackets { get; }
    public byte[] SendPayload { get; }
    public byte[] RecvPartial { get; }
}

internal sealed class ConnectResult
{
    public required List<ClientConn> Clients { get; init; }
    public required long Success { get; init; }
    public required long Fail { get; init; }
    public required long Timeout { get; init; }
}

internal enum IoTryResult
{
    Success,
    WouldBlock,
    Error,
    Cancelled
}

internal static class TimeUtil
{
    private static readonly double NsPerTick = 1_000_000_000.0 / Stopwatch.Frequency;

    public static ulong NowNs()
    {
        return (ulong)(Stopwatch.GetTimestamp() * NsPerTick);
    }
}

internal sealed class StreamEchoServer : IDisposable
{
    private readonly Config _cfg;
    private readonly bool _echoEnabled;
    private readonly ErrorBag _errors;

    private readonly CancellationTokenSource _cts = new();
    private Thread? _thread;
    private Context? _context;
    private Socket? _socket;

    public StreamEchoServer(Config cfg, bool echoEnabled, ErrorBag errors)
    {
        _cfg = cfg;
        _echoEnabled = echoEnabled;
        _errors = errors;
    }

    public bool Start()
    {
        try
        {
            _context = new Context();
            _context.SetOption(ContextOption.IoThreads, Math.Max(1, _cfg.IoThreads));
            _context.SetOption(ContextOption.MaxSockets, Math.Max(1024, _cfg.Ccu + 512));

            _socket = new Socket(_context, SocketType.Stream);
            Program.ConfigureStreamSocket(_socket, _cfg, forBind: true);
            _socket.Bind(Program.MakeEndpoint(_cfg));

            _thread = new Thread(Run)
            {
                IsBackground = true,
                Name = "stream-net-zlink-server"
            };
            _thread.Start();
            return true;
        }
        catch (Exception ex)
        {
            _errors.RecordException(ex);
            return false;
        }
    }

    public void Dispose()
    {
        _cts.Cancel();
        if (_thread is not null && _thread.IsAlive)
        {
            _thread.Join(TimeSpan.FromSeconds(2));
        }

        try
        {
            _socket?.Dispose();
        }
        catch
        {
            // no-op
        }

        try
        {
            _context?.Dispose();
        }
        catch
        {
            // no-op
        }

        _cts.Dispose();
    }

    private void Run()
    {
        if (_socket is null)
        {
            return;
        }

        var idBuf = new byte[256];
        var payloadBuf = new byte[Math.Max(4 * 1024 * 1024, (4 + Math.Max(9, _cfg.Size)) * 128)];
        var hasPendingRoutingId = false;
        var pendingRoutingIdLen = 0;

        while (!_cts.IsCancellationRequested)
        {
            if (!hasPendingRoutingId)
            {
                if (!Program.TryReceivePartWithRetry(_socket, idBuf, timeoutMs: 10, _cts.Token, _errors, out var idLen))
                {
                    continue;
                }

                if (idLen <= 0)
                {
                    continue;
                }

                pendingRoutingIdLen = idLen;
                hasPendingRoutingId = true;
            }

            if (!Program.TryReceivePartWithRetry(_socket, payloadBuf, timeoutMs: 10, _cts.Token, _errors, out var payloadLen))
            {
                continue;
            }

            hasPendingRoutingId = false;
            if (payloadLen <= 0)
            {
                continue;
            }

            if (payloadLen == 1 && (payloadBuf[0] == 0x01 || payloadBuf[0] == 0x00))
            {
                continue;
            }

            if (!_echoEnabled)
            {
                continue;
            }

            if (!Program.TrySendPartUntilSuccess(_socket, idBuf.AsSpan(0, pendingRoutingIdLen), SendFlags.SendMore, _cts.Token, _errors))
            {
                break;
            }

            if (!Program.TrySendPartUntilSuccess(_socket, payloadBuf.AsSpan(0, payloadLen), SendFlags.None, _cts.Token, _errors))
            {
                break;
            }
        }
    }
}

internal static class Program
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    internal const int ErrnoEintr = 4;
    internal const int ErrnoEagain = 11;

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
        cfg.ConnectRetries = Math.Max(1, cfg.ConnectRetries);
        cfg.ConnectRetryDelayMs = Math.Max(0, cfg.ConnectRetryDelayMs);
        cfg.IoThreads = Math.Max(1, cfg.IoThreads);
        cfg.Shards = Math.Max(1, cfg.Shards);
        cfg.SendBatch = Math.Max(1, cfg.SendBatch);
        cfg.LatencySampleRate = Math.Max(0, cfg.LatencySampleRate);
        cfg.Role = cfg.Role.Trim().ToLowerInvariant();

        if (cfg.Role is not ("both" or "server" or "client"))
        {
            PrintUsage();
            return 2;
        }

        TuneThreadPool(cfg);

        var row = new ResultRow();
        FillCommonRow(row, cfg);

        if (cfg.Transport != "tcp")
        {
            row.PassFail = "SKIP";
            PrintRow(row);
            AppendCsv(cfg.MetricsCsv, row);
            WriteSummaryJson(cfg.SummaryJson, row);
            Console.Error.WriteLine($"[stream-net-zlink] transport '{cfg.Transport}' skipped (tcp only)");
            return 0;
        }

        var ok = false;
        if (cfg.Scenario == "s0")
        {
            if (cfg.Role != "both")
            {
                row.PassFail = "FAIL";
            }
            else
            {
                ok = await RunS0Async(cfg, row).ConfigureAwait(false);
            }
        }
        else if (cfg.Scenario == "s1")
        {
            if (cfg.Role == "server")
            {
                ok = await RunServerOnlyAsync(cfg, row, echoEnabled: false).ConfigureAwait(false);
            }
            else
            {
                ok = await RunS1OrS2Async(cfg, row, withSend: false, startServer: cfg.Role == "both").ConfigureAwait(false);
            }
        }
        else
        {
            if (cfg.Role == "server")
            {
                ok = await RunServerOnlyAsync(cfg, row, echoEnabled: true).ConfigureAwait(false);
            }
            else
            {
                ok = await RunS1OrS2Async(cfg, row, withSend: true, startServer: cfg.Role == "both").ConfigureAwait(false);
            }
        }

        PrintRow(row);
        AppendCsv(cfg.MetricsCsv, row);
        WriteSummaryJson(cfg.SummaryJson, row);
        return ok ? 0 : 2;
    }

    private static Task<bool> RunS0Async(Config cfg, ResultRow row)
    {
        FillCommonRow(row, cfg);

        var errors = new ErrorBag();
        using var server = new StreamEchoServer(cfg, echoEnabled: true, errors);
        if (!server.Start())
        {
            row.PassFail = "FAIL";
            row.ErrorsByErrno = errors.SnapshotSorted();
            return Task.FromResult(false);
        }

        using var context = new Context();
        context.SetOption(ContextOption.IoThreads, Math.Max(1, cfg.IoThreads));
        context.SetOption(ContextOption.MaxSockets, Math.Max(1024, cfg.Ccu + 512));

        Socket? client = null;
        try
        {
            client = new Socket(context, SocketType.Stream);
            ConfigureStreamSocket(client, cfg, forBind: false);
            client.Connect(MakeEndpoint(cfg));

            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(cfg.ConnectTimeoutSec));
            if (!TryWaitStreamConnectEvent(client, cfg.ConnectTimeoutSec * 1000, cts.Token, errors, out var serverRid))
            {
                row.ConnectFail = 1;
                row.PassFail = "FAIL";
                row.ErrorsByErrno = errors.SnapshotSorted();
                return Task.FromResult(false);
            }

            var payload = new byte[Math.Max(9, cfg.Size)];
            payload[0] = 1;
            BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(1, 8), TimeUtil.NowNs());
            payload.AsSpan(9).Fill(0x44);

            if (!TrySendPartWithRetry(client, serverRid, SendFlags.SendMore, 1000, cts.Token, errors)
                || !TrySendPartWithRetry(client, payload, SendFlags.None, 1000, cts.Token, errors))
            {
                row.ConnectFail = 1;
                row.PassFail = "FAIL";
                row.ErrorsByErrno = errors.SnapshotSorted();
                return Task.FromResult(false);
            }

            var recvRid = new byte[256];
            var recvPayload = new byte[Math.Max(payload.Length, 256)];
            if (!TryReceivePartWithRetry(client, recvRid, 1000, cts.Token, errors, out var recvRidLen)
                || !TryReceivePartWithRetry(client, recvPayload, 1000, cts.Token, errors, out var recvLen)
                || recvRidLen <= 0)
            {
                row.ConnectFail = 1;
                row.PassFail = "FAIL";
                row.ErrorsByErrno = errors.SnapshotSorted();
                return Task.FromResult(false);
            }

            var ok = recvLen == payload.Length && recvPayload[0] == 1;
            row.ConnectSuccess = ok ? 1 : 0;
            row.ConnectFail = ok ? 0 : 1;
            row.Sent = ok ? 1 : 0;
            row.Recv = ok ? 1 : 0;
            row.PassFail = ok ? "PASS" : "FAIL";
            row.ErrorsByErrno = errors.SnapshotSorted();
            return Task.FromResult(ok);
        }
        catch (Exception ex)
        {
            errors.RecordException(ex);
            row.PassFail = "FAIL";
            row.ErrorsByErrno = errors.SnapshotSorted();
            return Task.FromResult(false);
        }
        finally
        {
            try
            {
                client?.Dispose();
            }
            catch
            {
                // no-op
            }
        }
    }

    private static async Task<bool> RunS1OrS2Async(Config cfg, ResultRow row, bool withSend, bool startServer)
    {
        FillCommonRow(row, cfg);

        var errors = new ErrorBag();
        var state = new BenchmarkState(cfg);
        var endpoints = BuildShardEndpoints(cfg);
        var servers = new List<StreamEchoServer>();

        if (startServer)
        {
            for (var shard = 0; shard < endpoints.Count; shard++)
            {
                var shardCfg = BuildShardConfig(cfg, cfg.Port + shard, endpoints.Count);
                var server = new StreamEchoServer(shardCfg, withSend, errors);
                if (!server.Start())
                {
                    server.Dispose();
                    foreach (var started in servers)
                    {
                        started.Dispose();
                    }

                    row.PassFail = "FAIL";
                    row.ErrorsByErrno = errors.SnapshotSorted();
                    return false;
                }

                servers.Add(server);
            }
        }

        try
        {
            using var context = new Context();
            context.SetOption(ContextOption.IoThreads, Math.Max(1, cfg.IoThreads));
            context.SetOption(ContextOption.MaxSockets, Math.Max(1024, cfg.Ccu + 512));

            var connectResult = await ConnectClientsAsync(cfg, context, errors, endpoints).ConfigureAwait(false);
            var clients = connectResult.Clients;
            row.ConnectSuccess = connectResult.Success;
            row.ConnectFail = connectResult.Fail;
            row.ConnectTimeout = connectResult.Timeout;

            var ok = row.ConnectSuccess == cfg.Ccu && row.ConnectFail == 0 && row.ConnectTimeout == 0;
            if (!withSend || !ok)
            {
                foreach (var client in clients)
                {
                    try
                    {
                        client.Socket.Dispose();
                    }
                    catch
                    {
                        // no-op
                    }
                }

                row.PassFail = ok ? "PASS" : "FAIL";
                row.ErrorsByErrno = errors.SnapshotSorted();
                return ok;
            }

            var runCts = new CancellationTokenSource();
            var runToken = runCts.Token;
            var startGate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var workerCount = ResolveTrafficWorkerCount(cfg, clients.Count);
            var shards = BuildTrafficShards(clients, workerCount);
            var trafficTasks = new List<Task>(shards.Count);
            foreach (var shard in shards)
            {
                trafficTasks.Add(RunTrafficShardAsync(shard, cfg, state, errors, startGate.Task, runToken));
            }

            state.SetPhase(sendEnabled: true, measureEnabled: false);
            startGate.TrySetResult();

            if (cfg.WarmupSec > 0)
            {
                await Task.Delay(TimeSpan.FromSeconds(cfg.WarmupSec)).ConfigureAwait(false);
            }

            // Drain warmup traffic before measurement so phase-0 backlog does not skew metrics.
            state.SetPhase(sendEnabled: false, measureEnabled: false);
            var warmupDrainDeadline = DateTime.UtcNow.AddSeconds(Math.Max(1, cfg.DrainTimeoutSec));
            while (state.PendingTotal > 0 && DateTime.UtcNow < warmupDrainDeadline)
            {
                await Task.Delay(5).ConfigureAwait(false);
            }

            if (state.PendingTotal > 0)
            {
                row.DrainTimeoutCount = 1;
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
        finally
        {
            foreach (var server in servers)
            {
                server.Dispose();
            }
        }
    }

    private static async Task<bool> RunServerOnlyAsync(Config cfg, ResultRow row, bool echoEnabled)
    {
        FillCommonRow(row, cfg);
        var errors = new ErrorBag();
        var servers = new List<StreamEchoServer>();
        var shardCount = Math.Max(1, cfg.Shards);

        for (var shard = 0; shard < shardCount; shard++)
        {
            var shardCfg = BuildShardConfig(cfg, cfg.Port + shard, shardCount);
            var server = new StreamEchoServer(shardCfg, echoEnabled, errors);
            if (!server.Start())
            {
                server.Dispose();
                foreach (var started in servers)
                {
                    started.Dispose();
                }

                row.PassFail = "FAIL";
                row.ErrorsByErrno = errors.SnapshotSorted();
                return false;
            }

            servers.Add(server);
        }

        using var stopCts = new CancellationTokenSource();
        PosixSignalRegistration? sigIntReg = null;
        PosixSignalRegistration? sigTermReg = null;
        if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
        {
            sigIntReg = PosixSignalRegistration.Create(PosixSignal.SIGINT, context =>
            {
                context.Cancel = true;
                stopCts.Cancel();
            });

            sigTermReg = PosixSignalRegistration.Create(PosixSignal.SIGTERM, context =>
            {
                context.Cancel = true;
                stopCts.Cancel();
            });
        }

        ConsoleCancelEventHandler cancelHandler = (_, e) =>
        {
            e.Cancel = true;
            stopCts.Cancel();
        };

        Console.CancelKeyPress += cancelHandler;
        try
        {
            await Task.Delay(Timeout.InfiniteTimeSpan, stopCts.Token).ConfigureAwait(false);
        }
        catch (TaskCanceledException)
        {
            // expected on Ctrl+C
        }
        finally
        {
            Console.CancelKeyPress -= cancelHandler;
            sigIntReg?.Dispose();
            sigTermReg?.Dispose();
            foreach (var server in servers)
            {
                server.Dispose();
            }
        }

        row.ErrorsByErrno = errors.SnapshotSorted();
        row.PassFail = row.ErrorsByErrno.Count == 0 ? "PASS" : "FAIL";
        return row.PassFail == "PASS";
    }

    private static async Task<ConnectResult> ConnectClientsAsync(Config cfg, Context context, ErrorBag errors, IReadOnlyList<string> endpoints)
    {
        var success = 0L;
        var fail = 0L;
        var timeout = 0L;
        var sockets = new ConcurrentBag<ClientConn>();
        var endpointCount = Math.Max(1, endpoints.Count);

        using var sem = new SemaphoreSlim(Math.Max(1, Math.Min(cfg.ConnectConcurrency, cfg.Ccu)));
        var tasks = new List<Task>(cfg.Ccu);

        for (var i = 0; i < cfg.Ccu; i++)
        {
            var connectIndex = i;
            await sem.WaitAsync().ConfigureAwait(false);
            tasks.Add(Task.Run(() =>
            {
                Socket? socket = null;
                try
                {
                    socket = new Socket(context, SocketType.Stream);
                    ConfigureStreamSocket(socket, cfg, forBind: false);
                    socket.Connect(endpoints[connectIndex % endpointCount]);

                    using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(Math.Max(1, cfg.ConnectTimeoutSec)));
                    if (!TryWaitStreamConnectEvent(socket, cfg.ConnectTimeoutSec * 1000, cts.Token, errors, out var serverRid))
                    {
                        Interlocked.Increment(ref timeout);
                        socket.Dispose();
                        socket = null;
                    }
                    else
                    {
                        sockets.Add(new ClientConn
                        {
                            Socket = socket,
                            PeerRoutingId = serverRid
                        });

                        Interlocked.Increment(ref success);
                        socket = null;
                    }
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
                        try
                        {
                            socket.Dispose();
                        }
                        catch
                        {
                            // no-op
                        }
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

    private static int ResolveTrafficWorkerCount(Config cfg, int clientCount)
    {
        if (clientCount <= 1)
        {
            return 1;
        }

        var configured = Math.Max(1, cfg.IoThreads);
        var cpuCap = Math.Max(1, Environment.ProcessorCount * 4);
        var target = Math.Max(configured, cpuCap);
        return Math.Max(1, Math.Min(clientCount, target));
    }

    private static List<List<ClientConn>> BuildTrafficShards(List<ClientConn> clients, int workerCount)
    {
        var count = Math.Max(1, Math.Min(workerCount, clients.Count));
        var shards = new List<List<ClientConn>>(count);
        for (var i = 0; i < count; i++)
        {
            shards.Add(new List<ClientConn>());
        }

        for (var i = 0; i < clients.Count; i++)
        {
            shards[i % count].Add(clients[i]);
        }

        return shards;
    }

    private static async Task RunTrafficShardAsync(
        List<ClientConn> shardClients,
        Config cfg,
        BenchmarkState state,
        ErrorBag errors,
        Task startGate,
        CancellationToken token)
    {
        var connStates = shardClients.Select(client => new TrafficConnState(client, cfg.Size, cfg.Inflight)).ToList();
        var recvRidScratch = new byte[256];
        var recvPayloadScratch = new byte[Math.Max(4 * 1024 * 1024, (4 + Math.Max(9, cfg.Size)) * 128)];

        try
        {
            await startGate.ConfigureAwait(false);

            while (!token.IsCancellationRequested)
            {
                var madeProgress = false;
                var sendEnabled = state.SendEnabled;

                foreach (var connState in connStates)
                {
                    if (token.IsCancellationRequested)
                    {
                        break;
                    }

                    if (connState.PendingSendBytes > 0)
                    {
                        var pendingSendRc = TrySendPendingPayload(connState, state, token, errors, out var progressed);
                        if (progressed)
                        {
                            madeProgress = true;
                        }

                        if (pendingSendRc == IoTryResult.Error)
                        {
                            return;
                        }
                    }

                    var sendBurst = 0;
                    while (sendEnabled
                           && connState.PendingSendBytes == 0
                           && connState.Pending < cfg.Inflight
                           && sendBurst < 2)
                    {
                        var newSendRc = TrySendNewPayload(connState, state, token, errors, out var progressed);
                        if (progressed)
                        {
                            madeProgress = true;
                        }

                        if (newSendRc == IoTryResult.Error || newSendRc == IoTryResult.Cancelled)
                        {
                            return;
                        }

                        if (newSendRc != IoTryResult.Success)
                        {
                            break;
                        }

                        sendBurst += 1;
                    }

                    var recvBurst = 0;
                    while ((connState.Pending > 0 || connState.RecvPayloadPending) && recvBurst < 16)
                    {
                        var recvRc = TryReceiveEcho(connState, state, recvRidScratch, recvPayloadScratch, token, errors, out var progressed);
                        if (progressed)
                        {
                            madeProgress = true;
                        }

                        if (recvRc == IoTryResult.Error)
                        {
                            return;
                        }

                        if (recvRc != IoTryResult.Success)
                        {
                            break;
                        }

                        recvBurst += 1;
                    }
                }

                if (!state.SendEnabled && connStates.All(conn => conn.Pending == 0 && conn.PendingSendBytes == 0 && !conn.RecvPayloadPending))
                {
                    break;
                }

                if (!madeProgress)
                {
                    Thread.SpinWait(256);
                }
            }
        }
        catch (Exception ex)
        {
            if (!token.IsCancellationRequested)
            {
                errors.RecordException(ex);
            }
        }
        finally
        {
            var remainingPending = 0;
            foreach (var connState in connStates)
            {
                remainingPending += connState.Pending;
                try
                {
                    connState.Conn.Socket.Dispose();
                }
                catch
                {
                    // no-op
                }
            }

            state.DropPending(remainingPending);
        }
    }

    private static IoTryResult TrySendNewPayload(
        TrafficConnState connState,
        BenchmarkState state,
        CancellationToken token,
        ErrorBag errors,
        out bool progressed)
    {
        progressed = false;

        if (!BuildSendBatchPayload(connState, state, out var phase, out var packets, out var payloadBytes))
        {
            return IoTryResult.WouldBlock;
        }

        connState.PendingSendPhase = phase;
        connState.PendingSendPacketCount = packets;
        connState.PendingSendBytes = payloadBytes;
        connState.PendingSendRoutingSent = false;
        progressed = true;

        var payloadRc = TrySendPendingPayload(connState, state, token, errors, out var payloadProgressed);
        progressed = progressed || payloadProgressed;
        return payloadRc;
    }

    private static IoTryResult TrySendPendingPayload(
        TrafficConnState connState,
        BenchmarkState state,
        CancellationToken token,
        ErrorBag errors,
        out bool progressed)
    {
        progressed = false;
        if (connState.PendingSendBytes <= 0)
        {
            return IoTryResult.WouldBlock;
        }

        if (!connState.PendingSendRoutingSent)
        {
            var ridRc = TrySendPartNonBlocking(connState.Conn.Socket, connState.Conn.PeerRoutingId, SendFlags.SendMore, token, errors);
            if (ridRc != IoTryResult.Success)
            {
                return ridRc;
            }

            connState.PendingSendRoutingSent = true;
            progressed = true;
        }

        var payloadRc = TrySendPartNonBlocking(connState.Conn.Socket, connState.SendPayload.AsSpan(0, connState.PendingSendBytes), SendFlags.None, token, errors);
        if (payloadRc != IoTryResult.Success)
        {
            return payloadRc;
        }

        connState.PendingSendRoutingSent = false;
        connState.Pending += connState.PendingSendPacketCount;
        for (var i = 0; i < connState.PendingSendPacketCount; i++)
        {
            state.OnSent(connState.PendingSendPhase);
        }

        connState.PendingSendBytes = 0;
        connState.PendingSendPacketCount = 0;
        progressed = true;
        return IoTryResult.Success;
    }

    private static bool BuildSendBatchPayload(
        TrafficConnState connState,
        BenchmarkState state,
        out int phase,
        out int packets,
        out int payloadBytes)
    {
        phase = state.CurrentPhase;
        packets = Math.Min(connState.InflightLimit - connState.Pending, connState.MaxBatchPackets);
        if (packets <= 0)
        {
            payloadBytes = 0;
            return false;
        }

        for (var i = 0; i < packets; i++)
        {
            var offset = i * connState.PacketSize;
            connState.SendPayload[offset + 4] = (byte)phase;
            BinaryPrimitives.WriteUInt64LittleEndian(connState.SendPayload.AsSpan(offset + 5, 8), TimeUtil.NowNs());
        }

        payloadBytes = packets * connState.PacketSize;
        return true;
    }

    private static IoTryResult TryReceiveEcho(
        TrafficConnState connState,
        BenchmarkState state,
        byte[] recvRidScratch,
        byte[] recvPayloadScratch,
        CancellationToken token,
        ErrorBag errors,
        out bool progressed)
    {
        progressed = false;

        if (!connState.RecvPayloadPending)
        {
            var ridRc = TryReceivePartNonBlocking(connState.Conn.Socket, recvRidScratch, token, errors, out var ridLen);
            if (ridRc != IoTryResult.Success)
            {
                return ridRc;
            }

            if (ridLen <= 0)
            {
                return IoTryResult.WouldBlock;
            }

            connState.RecvPayloadPending = true;
            progressed = true;
        }

        var payloadRc = TryReceivePartNonBlocking(connState.Conn.Socket, recvPayloadScratch, token, errors, out var payloadLen);
        if (payloadRc != IoTryResult.Success)
        {
            return payloadRc;
        }

        connState.RecvPayloadPending = false;
        progressed = true;

        if (payloadLen == 1 && (recvPayloadScratch[0] == 0x01 || recvPayloadScratch[0] == 0x00))
        {
            return IoTryResult.Success;
        }

        ConsumeStreamPayload(connState, state, recvPayloadScratch.AsSpan(0, payloadLen));
        return IoTryResult.Success;
    }

    private static void ConsumeStreamPayload(TrafficConnState connState, BenchmarkState state, ReadOnlySpan<byte> payload)
    {
        if (payload.Length == 1 && (payload[0] == 0x01 || payload[0] == 0x00))
        {
            return;
        }

        if (connState.RecvPartialLen > 0)
        {
            var need = connState.PacketSize - connState.RecvPartialLen;
            var take = Math.Min(need, payload.Length);
            payload.Slice(0, take).CopyTo(connState.RecvPartial.AsSpan(connState.RecvPartialLen));
            connState.RecvPartialLen += take;
            payload = payload.Slice(take);

            if (connState.RecvPartialLen == connState.PacketSize)
            {
                ConsumePacket(connState, state, connState.RecvPartial.AsSpan(0, connState.PacketSize));
                connState.RecvPartialLen = 0;
            }
        }

        while (payload.Length >= connState.PacketSize)
        {
            ConsumePacket(connState, state, payload.Slice(0, connState.PacketSize));
            payload = payload.Slice(connState.PacketSize);
        }

        if (payload.Length > 0)
        {
            payload.CopyTo(connState.RecvPartial);
            connState.RecvPartialLen = payload.Length;
        }
    }

    private static void ConsumePacket(TrafficConnState connState, BenchmarkState state, ReadOnlySpan<byte> packet)
    {
        if (packet.Length < connState.PacketSize)
        {
            return;
        }

        var bodySize = BinaryPrimitives.ReadUInt32BigEndian(packet.Slice(0, 4));
        if (bodySize != (uint)connState.PacketBodySize)
        {
            return;
        }

        if (connState.Pending > 0)
        {
            connState.Pending -= 1;
        }

        var phaseRecv = 0;
        ulong sentNs = 0;
        if (packet.Length >= 13)
        {
            phaseRecv = packet[4];
            sentNs = BinaryPrimitives.ReadUInt64LittleEndian(packet.Slice(5, 8));
        }

        state.OnRecv(phaseRecv, sentNs);
    }

    private static bool TryWaitStreamConnectEvent(Socket socket, int timeoutMs, CancellationToken token, ErrorBag errors, out byte[] peerRoutingId)
    {
        peerRoutingId = Array.Empty<byte>();

        var idBuf = new byte[256];
        var payload = new byte[16];

        var sw = Stopwatch.StartNew();
        while (!token.IsCancellationRequested && sw.ElapsedMilliseconds < timeoutMs)
        {
            if (!TryReceivePartWithRetry(socket, idBuf, timeoutMs: 10, token, errors, out var idLen))
            {
                continue;
            }

            if (!TryReceivePartWithRetry(socket, payload, timeoutMs: 10, token, errors, out var payloadLen))
            {
                continue;
            }

            if (payloadLen == 1 && payload[0] == 0x01 && idLen > 0)
            {
                peerRoutingId = idBuf.AsSpan(0, idLen).ToArray();
                return true;
            }
        }

        return false;
    }

    internal static IoTryResult TrySendPartNonBlocking(Socket socket, ReadOnlySpan<byte> data, SendFlags flags, CancellationToken token, ErrorBag errors)
    {
        if (token.IsCancellationRequested)
        {
            return IoTryResult.Cancelled;
        }

        if (socket.TrySend(data, out _, out var errno, flags | SendFlags.DontWait))
        {
            return IoTryResult.Success;
        }

        if (errno == ErrnoEagain || errno == ErrnoEintr)
        {
            return IoTryResult.WouldBlock;
        }

        errors.RecordErrno(errno);
        return IoTryResult.Error;
    }

    internal static IoTryResult TryReceivePartNonBlocking(Socket socket, Span<byte> data, CancellationToken token, ErrorBag errors, out int bytes)
    {
        bytes = 0;
        if (token.IsCancellationRequested)
        {
            return IoTryResult.Cancelled;
        }

        if (socket.TryReceive(data, out bytes, out var errno, ReceiveFlags.DontWait))
        {
            return IoTryResult.Success;
        }

        if (errno == ErrnoEagain || errno == ErrnoEintr)
        {
            return IoTryResult.WouldBlock;
        }

        errors.RecordErrno(errno);
        return IoTryResult.Error;
    }

    internal static bool TrySendPartUntilSuccess(Socket socket, ReadOnlySpan<byte> data, SendFlags flags, CancellationToken token, ErrorBag errors)
    {
        var spin = 0;
        while (!token.IsCancellationRequested)
        {
            var rc = TrySendPartNonBlocking(socket, data, flags, token, errors);
            if (rc == IoTryResult.Success)
            {
                return true;
            }

            if (rc == IoTryResult.Error || rc == IoTryResult.Cancelled)
            {
                return false;
            }

            spin++;
            if ((spin % 64) == 0)
            {
                Thread.Sleep(0);
            }
            else
            {
                Thread.SpinWait(64);
            }
        }

        return false;
    }

    internal static bool TrySendPartWithRetry(Socket socket, ReadOnlySpan<byte> data, SendFlags flags, int timeoutMs, CancellationToken token, ErrorBag errors)
    {
        var sw = Stopwatch.StartNew();
        var spin = 0;

        while (!token.IsCancellationRequested && sw.ElapsedMilliseconds < timeoutMs)
        {
            if (socket.TrySend(data, out _, out var errno, flags | SendFlags.DontWait))
            {
                return true;
            }

            if (errno == ErrnoEagain || errno == ErrnoEintr)
            {
                spin++;
                if ((spin % 64) == 0)
                {
                    Thread.Sleep(0);
                }

                continue;
            }

            errors.RecordErrno(errno);
            return false;
        }

        return false;
    }

    internal static bool TryReceivePartWithRetry(Socket socket, Span<byte> data, int timeoutMs, CancellationToken token, ErrorBag errors, out int bytes)
    {
        bytes = 0;
        var sw = Stopwatch.StartNew();
        var spin = 0;

        while (!token.IsCancellationRequested && sw.ElapsedMilliseconds < timeoutMs)
        {
            if (socket.TryReceive(data, out bytes, out var errno, ReceiveFlags.DontWait))
            {
                return true;
            }

            if (errno == ErrnoEagain || errno == ErrnoEintr)
            {
                spin++;
                if ((spin % 64) == 0)
                {
                    Thread.Sleep(0);
                }

                continue;
            }

            errors.RecordErrno(errno);
            return false;
        }

        return false;
    }

    private static void FillCommonRow(ResultRow row, Config cfg)
    {
        row.ScenarioId = string.IsNullOrWhiteSpace(cfg.ScenarioIdOverride) ? cfg.Scenario : cfg.ScenarioIdOverride;
        row.Transport = cfg.Transport;
        row.Ccu = cfg.Ccu;
        row.Inflight = cfg.Inflight;
        row.Size = cfg.Size;
    }

    internal static string MakeEndpoint(Config cfg)
    {
        return $"{cfg.Transport}://{cfg.BindHost}:{cfg.Port}";
    }

    private static List<string> BuildShardEndpoints(Config cfg)
    {
        var count = Math.Max(1, cfg.Shards);
        var endpoints = new List<string>(count);
        for (var i = 0; i < count; i++)
        {
            endpoints.Add($"{cfg.Transport}://{cfg.BindHost}:{cfg.Port + i}");
        }

        return endpoints;
    }

    private static Config BuildShardConfig(Config cfg, int port, int shardCount)
    {
        var perShardIoThreads = Math.Max(1, cfg.IoThreads / Math.Max(1, shardCount));
        return new Config
        {
            Scenario = cfg.Scenario,
            Role = cfg.Role,
            Transport = cfg.Transport,
            BindHost = cfg.BindHost,
            Port = port,
            Ccu = cfg.Ccu,
            Size = cfg.Size,
            Inflight = cfg.Inflight,
            WarmupSec = cfg.WarmupSec,
            MeasureSec = cfg.MeasureSec,
            DrainTimeoutSec = cfg.DrainTimeoutSec,
            ConnectConcurrency = cfg.ConnectConcurrency,
            ConnectTimeoutSec = cfg.ConnectTimeoutSec,
            ConnectRetries = cfg.ConnectRetries,
            ConnectRetryDelayMs = cfg.ConnectRetryDelayMs,
            Backlog = cfg.Backlog,
            Hwm = cfg.Hwm,
            Sndbuf = cfg.Sndbuf,
            Rcvbuf = cfg.Rcvbuf,
            IoThreads = perShardIoThreads,
            Shards = 1,
            SendBatch = cfg.SendBatch,
            LatencySampleRate = cfg.LatencySampleRate,
            ScenarioIdOverride = cfg.ScenarioIdOverride,
            MetricsCsv = cfg.MetricsCsv,
            SummaryJson = cfg.SummaryJson
        };
    }

    internal static void ConfigureStreamSocket(Socket socket, Config cfg, bool forBind)
    {
        socket.SetOption(SocketOption.SndBuf, Math.Max(4096, cfg.Sndbuf));
        socket.SetOption(SocketOption.RcvBuf, Math.Max(4096, cfg.Rcvbuf));
        socket.SetOption(SocketOption.SndHwm, Math.Max(1000, cfg.Hwm));
        socket.SetOption(SocketOption.RcvHwm, Math.Max(1000, cfg.Hwm));
        socket.SetOption(SocketOption.SndTimeo, Math.Max(1, cfg.ConnectTimeoutSec * 1000));
        socket.SetOption(SocketOption.RcvTimeo, Math.Max(1, cfg.ConnectTimeoutSec * 1000));
        socket.SetOption(SocketOption.ConnectTimeout, Math.Max(1, cfg.ConnectTimeoutSec * 1000));
        if (forBind)
        {
            socket.SetOption(SocketOption.Backlog, Math.Max(32, cfg.Backlog));
        }
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
        Console.WriteLine("Usage: StreamZlinkScenario --scenario s0|s1|s2 [options]");
        Console.WriteLine("Options:");
        Console.WriteLine("  --transport tcp                  (net-zlink runner currently tcp only)");
        Console.WriteLine("  --role both|server|client        (default both)");
        Console.WriteLine("  --port N                         (default 27110)");
        Console.WriteLine("  --ccu N                          (default 10000)");
        Console.WriteLine("  --size N                         (default 1024)");
        Console.WriteLine("  --inflight N                     (per-connection, default 30)");
        Console.WriteLine("  --warmup N                       (default 3 sec)");
        Console.WriteLine("  --measure N                      (default 10 sec)");
        Console.WriteLine("  --drain-timeout N                (default 10 sec)");
        Console.WriteLine("  --connect-concurrency N          (default 256)");
        Console.WriteLine("  --connect-timeout N              (default 10 sec)");
        Console.WriteLine("  --connect-retries N              (accepted, default 3)");
        Console.WriteLine("  --connect-retry-delay-ms N       (accepted, default 100)");
        Console.WriteLine("  --backlog N                      (default 32768)");
        Console.WriteLine("  --hwm N                          (default 1000000)");
        Console.WriteLine("  --sndbuf N                       (default 262144)");
        Console.WriteLine("  --rcvbuf N                       (default 262144)");
        Console.WriteLine("  --io-threads N                   (default 1)");
        Console.WriteLine("  --shards N                       (default 1)");
        Console.WriteLine("  --send-batch N                   (accepted, default 1)");
        Console.WriteLine("  --latency-sample-rate N          (default 1, 0=disable latency)");
        Console.WriteLine("  --scenario-id ID                 override scenario_id output");
        Console.WriteLine("  --metrics-csv PATH               append row to csv");
        Console.WriteLine("  --summary-json PATH              write row json");
    }

    private static Config ParseConfig(string[] args)
    {
        return new Config
        {
            Scenario = ArgStr(args, "--scenario", "s0"),
            Role = ArgStr(args, "--role", "both"),
            Transport = ArgStr(args, "--transport", "tcp"),
            BindHost = ArgStr(args, "--bind-host", "127.0.0.1"),
            Port = ArgInt(args, "--port", 27110),
            Ccu = ArgInt(args, "--ccu", 10000),
            Size = ArgInt(args, "--size", 1024),
            Inflight = ArgInt(args, "--inflight", 30),
            WarmupSec = ArgInt(args, "--warmup", 3),
            MeasureSec = ArgInt(args, "--measure", 10),
            DrainTimeoutSec = ArgInt(args, "--drain-timeout", 10),
            ConnectConcurrency = ArgInt(args, "--connect-concurrency", 256),
            ConnectTimeoutSec = ArgInt(args, "--connect-timeout", 10),
            ConnectRetries = ArgInt(args, "--connect-retries", 3),
            ConnectRetryDelayMs = ArgInt(args, "--connect-retry-delay-ms", 100),
            Backlog = ArgInt(args, "--backlog", 32768),
            Hwm = ArgInt(args, "--hwm", 1000000),
            Sndbuf = ArgInt(args, "--sndbuf", 256 * 1024),
            Rcvbuf = ArgInt(args, "--rcvbuf", 256 * 1024),
            IoThreads = ArgInt(args, "--io-threads", 1),
            Shards = ArgInt(args, "--shards", 1),
            SendBatch = ArgInt(args, "--send-batch", 1),
            LatencySampleRate = ArgInt(args, "--latency-sample-rate", 1),
            ScenarioIdOverride = ArgStr(args, "--scenario-id", ""),
            MetricsCsv = ArgStr(args, "--metrics-csv", ""),
            SummaryJson = ArgStr(args, "--summary-json", "")
        };
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
        var connectFloor = Math.Max(64, cfg.ConnectConcurrency);
        var trafficFloor = Math.Max(Environment.ProcessorCount * 4, cfg.IoThreads * 8);
        var desiredWorker = Math.Max(worker, Math.Max(connectFloor, trafficFloor));
        desiredWorker = Math.Min(desiredWorker, 1024);
        ThreadPool.SetMinThreads(desiredWorker, io);
    }
}

using System.Diagnostics;
using System.Net;
using System.Net.Http.Json;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Google.Protobuf;
using Grpc.Net.Client;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using WithGrpcBench.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Channels;

AppContext.SetSwitch("System.Net.Http.SocketsHttpHandler.Http2UnencryptedSupport", true);

var options = BenchOptions.Parse(args);
options.Validate();
ConfigureThreadPool(options);
Directory.CreateDirectory(options.Output);

using var grpcHandler = new SocketsHttpHandler
{
    EnableMultipleHttp2Connections = false,
    MaxConnectionsPerServer = 1
};
using var grpcChannel = GrpcChannel.ForAddress(options.GrpcUrl, new GrpcChannelOptions
{
    HttpHandler = grpcHandler
});
var grpc = new BenchService.BenchServiceClient(grpcChannel);

var zlinkBuilder = Host.CreateApplicationBuilder(args);
ConfigureQuietLogging(zlinkBuilder);
zlinkBuilder.Services.AddZLinkFramework(framework =>
{
    framework.Codecs.Use(ZLinkProtobufCodec.Default);
    framework.AddClientServerChannel("bench")
        .EnableClient(options.ZLinkEndpoint);
});

using var zlinkHost = zlinkBuilder.Build();
await zlinkHost.StartAsync();
var zlink = zlinkHost.Services.GetRequiredService<IZLinkChannelClient>();
using var rawContext = Systems.Zlink.Zlink.CreateContext();
var metadata = await CreateMetadataAsync(options);

var results = new List<BenchResult>();
foreach (var payloadSize in options.PayloadSizes)
{
    if (options.Scenario is "all" or "request" or "request-serial")
    {
        Console.Error.WriteLine($"[bench] request payload={payloadSize} mode=serial");
        Console.Error.WriteLine("[bench] running grpc-dotnet-request-serial");
        results.Add(await RunRequestSerialAsync(
            "grpc-dotnet-request-serial",
            payloadSize,
            options,
            options.GrpcStatsUrl,
            async (payload, ct) =>
            {
                return await grpc.EchoAsync(payload, cancellationToken: ct);
            }));
        Console.Error.WriteLine("[bench] finished grpc-dotnet-request-serial");

        Console.Error.WriteLine("[bench] running zlink-framework-dotnet-request-serial");
        results.Add(await RunRequestSerialAsync(
            "zlink-framework-dotnet-request-serial",
            payloadSize,
            options,
            options.ZLinkStatsUrl,
            async (payload, ct) =>
            {
                return await zlink.RequestToChannel("bench", payload)
                    .PacketName("BenchPayload")
                    .Async<BenchPayload>(ct);
            }));
        Console.Error.WriteLine("[bench] finished zlink-framework-dotnet-request-serial");

        Console.Error.WriteLine("[bench] running zlink-dotnet-request-serial");
        results.Add(await RunRawRequestSerialAsync(rawContext, payloadSize, options));
        Console.Error.WriteLine("[bench] finished zlink-dotnet-request-serial");
    }

    if (options.Scenario is "all" or "request" or "request-saturation")
    {
        Console.Error.WriteLine($"[bench] request payload={payloadSize} mode=saturation");
        Console.Error.WriteLine("[bench] running grpc-dotnet-request-saturation");
        results.Add(await RunRequestAsync(
            "grpc-dotnet-request-saturation",
            payloadSize,
            options,
            options.GrpcStatsUrl,
            async (payload, ct) =>
            {
                return await grpc.EchoAsync(payload, cancellationToken: ct);
            }));
        Console.Error.WriteLine("[bench] finished grpc-dotnet-request-saturation");

        Console.Error.WriteLine("[bench] running zlink-framework-dotnet-request-saturation");
        results.Add(await RunRequestAsync(
            "zlink-framework-dotnet-request-saturation",
            payloadSize,
            options,
            options.ZLinkStatsUrl,
            async (payload, ct) =>
            {
                return await zlink.RequestToChannel("bench", payload)
                    .PacketName("BenchPayload")
                    .Async<BenchPayload>(ct);
            }));
        Console.Error.WriteLine("[bench] finished zlink-framework-dotnet-request-saturation");

        Console.Error.WriteLine("[bench] running zlink-dotnet-request-saturation");
        results.Add(await RunRawRequestAsync(rawContext, payloadSize, options));
        Console.Error.WriteLine("[bench] finished zlink-dotnet-request-saturation");
    }

    if (options.Scenario is "all" or "send" or "command" or "send-saturation")
    {
        Console.Error.WriteLine($"[bench] send payload={payloadSize} concurrency={options.SendConcurrency}");
        Console.Error.WriteLine("[bench] running grpc-dotnet-send-saturation");
        results.Add(await RunSendAsync(
            "grpc-dotnet-send-saturation",
            payloadSize,
            options,
            options.GrpcStatsUrl,
            async (_, payload, ct) => await grpc.CommandAsync(payload, cancellationToken: ct)));
        Console.Error.WriteLine("[bench] finished grpc-dotnet-send-saturation");

        Console.Error.WriteLine("[bench] running zlink-framework-dotnet-send-saturation");
        results.Add(await RunSendAsync(
            "zlink-framework-dotnet-send-saturation",
            payloadSize,
            options,
            options.ZLinkStatsUrl,
            (_, payload, ct) =>
            {
                zlink.SendToChannel("bench", payload)
                    .PacketName("BenchPayload")
                    .Submit(ct);
                return ValueTask.CompletedTask;
            }));
        Console.Error.WriteLine("[bench] finished zlink-framework-dotnet-send-saturation");

        Console.Error.WriteLine("[bench] running zlink-dotnet-send-saturation");
        results.Add(await RunRawSendAsync(rawContext, payloadSize, options));
        Console.Error.WriteLine("[bench] finished zlink-dotnet-send-saturation");
    }
}

Print(results);

var resultFile = Path.Combine(options.Output, "results.json");
var report = new BenchReport(metadata, results);
var jsonOptions = new JsonSerializerOptions
{
    WriteIndented = true,
    PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) }
};
await File.WriteAllTextAsync(
    resultFile,
    JsonSerializer.Serialize(report, jsonOptions));
await File.WriteAllTextAsync(options.ReportPath, FormatText(report));

await zlinkHost.StopAsync();

static async ValueTask<BenchResult> RunRequestSerialAsync(
    string name,
    int payloadSize,
    BenchOptions options,
    string statsUrl,
    Func<BenchPayload, CancellationToken, ValueTask<BenchPayload>> operation)
{
    using var cts = new CancellationTokenSource(options.Timeout);
    using var http = new HttpClient();

    for (var i = 0; i < options.Warmup; i++)
    {
        var payload = BenchMetricHeaders.CreatePayload(payloadSize, options.RunId, BenchPhase.Warmup, (ulong)i);
        var reply = await operation(payload, cts.Token);
        ValidateReply(reply, options.RunId, BenchPhase.Warmup, payloadSize, (ulong)i);
    }

    using var reset = await http.PostAsync($"{statsUrl}/bench/reset", null, cts.Token);
    reset.EnsureSuccessStatusCode();

    var samples = new List<long>();
    var errors = 0;
    var completed = 0;
    var resources = ResourceSample.Start();
    var total = Stopwatch.StartNew();
    var activeUntil = total.Elapsed + TimeSpan.FromSeconds(options.DurationSeconds);

    while (total.Elapsed < activeUntil)
    {
        var index = completed + errors;
        var payload = BenchMetricHeaders.CreatePayload(
            payloadSize,
            options.RunId,
            BenchPhase.Active,
            (ulong)index);
        var started = Stopwatch.GetTimestamp();
        try
        {
            var reply = await operation(payload, cts.Token);
            ValidateReply(reply, options.RunId, BenchPhase.Active, payloadSize, (ulong)index);
            completed++;
        }
        catch
        {
            errors++;
        }

        AddLatencySample(samples, ElapsedMicros(started, Stopwatch.GetTimestamp()), options.LatencySampleLimit);
    }

    total.Stop();
    var clientCpuSeconds = resources.CpuSeconds();
    var clientMemoryMb = resources.WorkingSetMb();
    var server = await http.GetFromJsonAsync<BenchServerSnapshot>($"{statsUrl}/bench/stats", cts.Token)
        ?? BenchServerSnapshot.Empty;

    return new BenchResult(
        name,
        "KOPS",
        payloadSize,
        options.DurationSeconds,
        completed,
        errors,
        server.Errors,
        options.Warmup,
        Math.Max(1.0, options.DurationSeconds),
        completed / Math.Max(1.0, options.DurationSeconds),
        PercentileSuccessful(samples, 0.95),
        PercentileSuccessful(samples, 0.99),
        MeanSuccessful(samples),
        null,
        null,
        null,
        clientCpuSeconds,
        clientMemoryMb,
        server.CpuSeconds,
        server.WorkingSetMb);
}

static async ValueTask<BenchResult> RunRequestAsync(
    string name,
    int payloadSize,
    BenchOptions options,
    string statsUrl,
    Func<BenchPayload, CancellationToken, ValueTask<BenchPayload>> operation)
{
    using var cts = new CancellationTokenSource(options.Timeout);
    using var http = new HttpClient();

    for (var i = 0; i < options.Warmup; i++)
    {
        var payload = BenchMetricHeaders.CreatePayload(payloadSize, options.RunId, BenchPhase.Warmup, (ulong)i);
        var reply = await operation(payload, cts.Token);
        ValidateReply(reply, options.RunId, BenchPhase.Warmup, payloadSize, (ulong)i);
    }

    using var reset = await http.PostAsync($"{statsUrl}/bench/reset", null, cts.Token);
    reset.EnsureSuccessStatusCode();

    var samples = new List<long>();
    var samplesGate = new object();
    var next = 0L;
    var errors = 0;
    var completed = 0;
    var outstanding = 0;
    var submittingStopped = 0;
    var allCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var resources = ResourceSample.Start();
    var total = Stopwatch.StartNew();
    var activeUntil = total.Elapsed + TimeSpan.FromSeconds(options.DurationSeconds);

    while (total.Elapsed < activeUntil)
    {
        var index = Interlocked.Increment(ref next) - 1;
        var payload = BenchMetricHeaders.CreatePayload(
            payloadSize,
            options.RunId,
            BenchPhase.Active,
            (ulong)index);
        var started = Stopwatch.GetTimestamp();
        Interlocked.Increment(ref outstanding);
        _ = CompleteRequestAsync(index, payload, started);
    }

    Volatile.Write(ref submittingStopped, 1);
    if (Volatile.Read(ref outstanding) == 0)
    {
        allCompleted.TrySetResult();
    }

    await allCompleted.Task.WaitAsync(cts.Token);
    total.Stop();
    var clientCpuSeconds = resources.CpuSeconds();
    var clientMemoryMb = resources.WorkingSetMb();
    var server = await http.GetFromJsonAsync<BenchServerSnapshot>($"{statsUrl}/bench/stats", cts.Token)
        ?? BenchServerSnapshot.Empty;

    return new BenchResult(
        name,
        "KOPS",
        payloadSize,
        options.DurationSeconds,
        completed,
        errors,
        server.Errors,
        options.Warmup,
        Math.Max(1.0, options.DurationSeconds),
        completed / Math.Max(1.0, options.DurationSeconds),
        PercentileSuccessful(samples, 0.95),
        PercentileSuccessful(samples, 0.99),
        MeanSuccessful(samples),
        null,
        null,
        null,
        clientCpuSeconds,
        clientMemoryMb,
        server.CpuSeconds,
        server.WorkingSetMb);

    async Task CompleteRequestAsync(long index, BenchPayload payload, long started)
    {
        try
        {
            var reply = await operation(payload, cts.Token);
            ValidateReply(reply, options.RunId, BenchPhase.Active, payloadSize, (ulong)index);
            Interlocked.Increment(ref completed);
        }
        catch
        {
            Interlocked.Increment(ref errors);
        }
        finally
        {
            AddLatencySampleLocked(
                samples,
                samplesGate,
                ElapsedMicros(started, Stopwatch.GetTimestamp()),
                options.LatencySampleLimit);
            if (Interlocked.Decrement(ref outstanding) == 0
                && Volatile.Read(ref submittingStopped) != 0)
            {
                allCompleted.TrySetResult();
            }
        }
    }
}

static void ConfigureQuietLogging(HostApplicationBuilder builder)
{
    builder.Logging.ClearProviders();
    builder.Logging.AddConsole();
    builder.Logging.SetMinimumLevel(LogLevel.Warning);
}

static void ConfigureThreadPool(BenchOptions options)
{
    ThreadPool.GetMinThreads(out var workerThreads, out var completionPortThreads);
    var requestedWorkers = Math.Max(
        workerThreads,
        options.SendConcurrency + Environment.ProcessorCount * 2);
    ThreadPool.SetMinThreads(requestedWorkers, completionPortThreads);
}

static async ValueTask<BenchResult> RunSendAsync(
    string name,
    int payloadSize,
    BenchOptions options,
    string statsUrl,
    Func<int, BenchPayload, CancellationToken, ValueTask> operation)
{
    using var cts = new CancellationTokenSource(options.Timeout);
    using var http = new HttpClient();

    for (var i = 0; i < options.Warmup; i++)
    {
        var payload = BenchMetricHeaders.CreatePayload(payloadSize, options.RunId, BenchPhase.Warmup, (ulong)i);
        await operation(0, payload, cts.Token);
    }

    using var reset = await http.PostAsync($"{statsUrl}/bench/reset", null, cts.Token);
    reset.EnsureSuccessStatusCode();

    var samples = new List<long>();
    var samplesGate = new object();
    var errors = 0;
    var sent = -1;
    var resources = ResourceSample.Start();
    var total = Stopwatch.StartNew();
    var activeUntil = total.Elapsed + TimeSpan.FromSeconds(options.DurationSeconds);

    var workers = Enumerable.Range(0, options.SendConcurrency)
        .Select(slot => Task.Run(async () =>
        {
            while (total.Elapsed < activeUntil)
            {
                var sequence = Interlocked.Increment(ref sent);
                var payload = BenchMetricHeaders.CreatePayload(
                    payloadSize,
                    options.RunId,
                    BenchPhase.Active,
                    (ulong)sequence);
                var started = Stopwatch.GetTimestamp();
                try
                {
                    await operation(slot, payload, cts.Token);
                }
                catch
                {
                    Interlocked.Increment(ref errors);
                }

                AddLatencySampleLocked(
                    samples,
                    samplesGate,
                    ElapsedMicros(started, Stopwatch.GetTimestamp()),
                    options.LatencySampleLimit);
            }
        }, cts.Token))
        .ToArray();

    await Task.WhenAll(workers);
    total.Stop();
    var clientCpuSeconds = resources.CpuSeconds();
    var clientMemoryMb = resources.WorkingSetMb();

    var attempted = sent + 1;
    var expectedServerMessages = Math.Max(0, attempted - errors);
    var server = await WaitForServerStatsAsync(http, statsUrl, expectedServerMessages, options.CommandSettleMs, cts.Token);
    var missing = Math.Max(0, attempted - errors - server.ActiveMessages);

    return new BenchResult(
        name,
        "KMSG/s",
        payloadSize,
        options.DurationSeconds,
        server.ActiveMessages,
        errors,
        server.Errors + missing,
        options.Warmup,
        Math.Max(1.0, options.DurationSeconds),
        server.ActiveMessages / Math.Max(1.0, options.DurationSeconds),
        PercentileSuccessful(samples, 0.95),
        PercentileSuccessful(samples, 0.99),
        MeanSuccessful(samples),
        server.MeanMicros,
        server.P95Micros,
        server.P99Micros,
        clientCpuSeconds,
        clientMemoryMb,
        server.CpuSeconds,
        server.WorkingSetMb);
}

static async ValueTask<BenchResult> RunRawSendAsync(
    IContext context,
    int payloadSize,
    BenchOptions options)
{
    using var socket = context.CreateDealerSocket();
    socket.SetRoutingId(RoutingId.From(
        Encoding.ASCII.GetBytes($"bench-send-{Environment.ProcessId}")));
    socket.Connect(options.ZLinkRawCommandEndpoint);

    return await RunSendAsync(
        "zlink-dotnet-send-saturation",
        payloadSize,
        options,
        options.ZLinkRawStatsUrl,
        (_, payload, _) =>
        {
            RawSend(socket, payload);
            return ValueTask.CompletedTask;
        });
}

static async ValueTask<BenchResult> RunRawRequestSerialAsync(
    IContext context,
    int payloadSize,
    BenchOptions options)
{
    using var socket = context.CreateDealerSocket();
    socket.SetRoutingId(RoutingId.From(
        Encoding.ASCII.GetBytes($"bench-request-serial-{Environment.ProcessId}")));
    socket.Connect(options.ZLinkRawEndpoint);

    return await RunRequestSerialAsync(
        "zlink-dotnet-request-serial",
        payloadSize,
        options,
        options.ZLinkRawStatsUrl,
        async (payload, ct) => await RawRequestCallbackAsync(socket, payload, ct));
}

static async ValueTask<BenchResult> RunRawRequestAsync(
    IContext context,
    int payloadSize,
    BenchOptions options)
{
    using var cts = new CancellationTokenSource(options.Timeout);
    using var http = new HttpClient();
    using var socket = context.CreateDealerSocket();
    socket.SetRoutingId(RoutingId.From(
        Encoding.ASCII.GetBytes($"bench-request-{Environment.ProcessId}")));
    socket.Connect(options.ZLinkRawEndpoint);

    for (var i = 0; i < options.Warmup; i++)
    {
        var payload = BenchMetricHeaders.CreatePayload(payloadSize, options.RunId, BenchPhase.Warmup, (ulong)i);
        var reply = await RawRequestCallbackAsync(socket, payload, cts.Token);
        ValidateReply(reply, options.RunId, BenchPhase.Warmup, payloadSize, (ulong)i);
    }

    using var reset = await http.PostAsync($"{options.ZLinkRawStatsUrl}/bench/reset", null, cts.Token);
    reset.EnsureSuccessStatusCode();

    var samples = new List<long>();
    var samplesGate = new object();
    var next = 0L;
    var completed = 0;
    var errors = 0;
    var outstanding = 0;
    var submittingStopped = 0;
    var allCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var resources = ResourceSample.Start();
    var total = Stopwatch.StartNew();
    var activeUntil = total.Elapsed + TimeSpan.FromSeconds(options.DurationSeconds);

    while (total.Elapsed < activeUntil)
    {
        var index = Interlocked.Increment(ref next) - 1;
        var started = Stopwatch.GetTimestamp();
        var payload = BenchMetricHeaders.CreatePayload(
            payloadSize,
            options.RunId,
            BenchPhase.Active,
            (ulong)index);

        Interlocked.Increment(ref outstanding);
        try
        {
            var accepted = SubmitRawRequestCallback(
                socket,
                payload,
                (result, parts) =>
                {
                    try
                    {
                        if (result == RequestResult.Ok)
                        {
                            Interlocked.Increment(ref completed);
                        }
                        else
                        {
                            Interlocked.Increment(ref errors);
                        }
                    }
                    catch
                    {
                        Interlocked.Increment(ref errors);
                    }
                    finally
                    {
                        foreach (var part in parts)
                        {
                            part.Dispose();
                        }

                        AddLatencySampleLocked(
                            samples,
                            samplesGate,
                            ElapsedMicros(started, Stopwatch.GetTimestamp()),
                            options.LatencySampleLimit);
                        if (Interlocked.Decrement(ref outstanding) == 0
                            && Volatile.Read(ref submittingStopped) != 0)
                        {
                            allCompleted.TrySetResult();
                        }
                    }
                });

            if (!accepted)
            {
                Interlocked.Increment(ref errors);
                if (Interlocked.Decrement(ref outstanding) == 0
                    && Volatile.Read(ref submittingStopped) != 0)
                {
                    allCompleted.TrySetResult();
                }
            }
        }
        catch
        {
            Interlocked.Increment(ref errors);
            if (Interlocked.Decrement(ref outstanding) == 0
                && Volatile.Read(ref submittingStopped) != 0)
            {
                allCompleted.TrySetResult();
            }
        }
    }

    Volatile.Write(ref submittingStopped, 1);
    if (Volatile.Read(ref outstanding) == 0)
    {
        allCompleted.TrySetResult();
    }

    await allCompleted.Task.WaitAsync(cts.Token);
    total.Stop();
    var clientCpuSeconds = resources.CpuSeconds();
    var clientMemoryMb = resources.WorkingSetMb();
    var server = await http.GetFromJsonAsync<BenchServerSnapshot>($"{options.ZLinkRawStatsUrl}/bench/stats", cts.Token)
        ?? BenchServerSnapshot.Empty;

    return new BenchResult(
        "zlink-dotnet-request-saturation",
        "KOPS",
        payloadSize,
        options.DurationSeconds,
        completed,
        errors,
        server.Errors,
        options.Warmup,
        Math.Max(1.0, options.DurationSeconds),
        completed / Math.Max(1.0, options.DurationSeconds),
        PercentileSuccessful(samples, 0.95),
        PercentileSuccessful(samples, 0.99),
        MeanSuccessful(samples),
        null,
        null,
        null,
        clientCpuSeconds,
        clientMemoryMb,
        server.CpuSeconds,
        server.WorkingSetMb);
}

static async ValueTask<BenchPayload> RawRequestCallbackAsync(
    IDealerSocket socket,
    BenchPayload payload,
    CancellationToken cancellationToken)
{
    var completion = new TaskCompletionSource<BenchPayload>(
        TaskCreationOptions.RunContinuationsAsynchronously);

    try
    {
        SubmitRawRequestCallback(
            socket,
            payload,
            (result, parts) =>
            {
                try
                {
                    if (result != RequestResult.Ok)
                    {
                        completion.TrySetException(
                            new InvalidOperationException($"Raw zlink request failed: {result}."));
                        return;
                    }

                    var replyPart = parts.Count == 1 ? parts[0] : parts[^1];
                    completion.TrySetResult(new BenchPayload
                    {
                        Body = ByteString.CopyFrom(replyPart.AsReadOnlySpan())
                    });
                }
                catch (Exception ex)
                {
                    completion.TrySetException(ex);
                }
                finally
                {
                    foreach (var part in parts)
                    {
                        part.Dispose();
                    }
                }
            });
    }
    catch (Exception ex)
    {
        completion.TrySetException(ex);
    }

    return await completion.Task.WaitAsync(cancellationToken);
}

static bool SubmitRawRequestCallback(
    IDealerSocket socket,
    BenchPayload payload,
    RequestCallback callback)
{
    var message = new Message(payload.Body.Memory);
    try
    {
        return socket.Request()
            .Message(message)
            .Submit(callback);
    }
    catch
    {
        throw;
    }
    finally
    {
        message.Dispose();
    }
}

static async ValueTask<BenchServerSnapshot> WaitForServerStatsAsync(
    HttpClient http,
    string statsUrl,
    long expectedServerMessages,
    int settleMs,
    CancellationToken cancellationToken)
{
    if (settleMs > 0)
    {
        await Task.Delay(settleMs, cancellationToken);
    }

	    var deadline = Stopwatch.GetTimestamp() + Stopwatch.Frequency * Math.Max(1, settleMs) / 1000;
	    var latest = BenchServerSnapshot.Empty;
	    while (Stopwatch.GetTimestamp() < deadline)
	    {
	        latest = await http.GetFromJsonAsync<BenchServerSnapshot>($"{statsUrl}/bench/stats", cancellationToken)
	            ?? latest;
	        if (latest.ActiveMessages + latest.Errors >= expectedServerMessages)
	        {
	            return latest;
	        }

	        await Task.Delay(10, cancellationToken);
	    }

	    return latest;
	}

static void ValidateReply(BenchPayload reply, uint runId, BenchPhase phase, int payloadSize, ulong sequence)
{
    BenchPayloads.Validate(reply, payloadSize);
    if (!BenchMetricHeaders.TryDecode(reply, out var header)
        || !BenchMetricHeaders.IsExpected(header, runId, phase, payloadSize, sequence))
    {
        throw new InvalidOperationException($"Echo reply did not carry the expected {phase} metric header.");
    }
}

static void RawSend(IDealerSocket socket, BenchPayload payload)
{
    var message = new Message(payload.Body.Memory);
    try
    {
        if (!socket.Send().Message(message).Submit())
        {
            message.Dispose();
            throw new InvalidOperationException("Raw zlink send was not accepted.");
        }
    }
    catch
    {
        throw;
    }
    finally
    {
        message.Dispose();
    }
}

static long ElapsedMicros(long started, long stopped)
{
    return (long)((stopped - started) * 1_000_000.0 / Stopwatch.Frequency);
}

static long Percentile(long[] sortedSamples, double percentile)
{
    if (sortedSamples.Length == 0) return 0;
    var index = (int)Math.Ceiling(percentile * sortedSamples.Length) - 1;
    return sortedSamples[Math.Clamp(index, 0, sortedSamples.Length - 1)];
}

static long PercentileSuccessful(List<long> samples, double percentile)
{
    var sorted = samples.Where(static sample => sample > 0).ToArray();
    Array.Sort(sorted);
    return Percentile(sorted, percentile);
}

static double MeanSuccessful(List<long> samples)
{
    var positives = samples.Where(static sample => sample > 0).ToArray();
    return positives.Length == 0 ? 0 : positives.Average();
}

static void AddLatencySample(List<long> samples, long elapsedMicros, int limit)
{
    if (samples.Count < limit)
    {
        samples.Add(elapsedMicros);
    }
}

static void AddLatencySampleLocked(List<long> samples, object gate, long elapsedMicros, int limit)
{
    lock (gate)
    {
        AddLatencySample(samples, elapsedMicros, limit);
    }
}

static void Print(IReadOnlyList<BenchResult> results)
{
    Console.Write(FormatText(new BenchReport(BenchReportMetadata.Empty, results)));
}

static string FormatText(BenchReport report)
{
    var builder = new StringBuilder();
    builder.AppendLine(".NET messaging local bench");
    if (!report.Metadata.IsEmpty)
    {
        builder.AppendLine();
        builder.AppendLine("Effective Options:");
        builder.AppendLine($"  generated_utc: {report.Metadata.GeneratedUtc:O}");
        builder.AppendLine($"  commit: {report.Metadata.Commit}");
        builder.AppendLine($"  cpu: {report.Metadata.Cpu}");
        builder.AppendLine($"  os: {report.Metadata.Os}");
        builder.AppendLine($"  dotnet_sdk: {report.Metadata.DotNetSdk}");
        builder.AppendLine($"  configuration: {report.Metadata.Configuration}");
        builder.AppendLine($"  payload_sizes: {string.Join(",", report.Metadata.PayloadSizes)}");
        builder.AppendLine("  request_mode: native-capacity");
        builder.AppendLine($"  send_concurrency: {report.Metadata.SendConcurrency}");
        builder.AppendLine($"  latency_sample_limit: {report.Metadata.LatencySampleLimit}");
        builder.AppendLine($"  warmup: {report.Metadata.Warmup}");
        builder.AppendLine($"  duration_seconds: {report.Metadata.DurationSeconds}");
        builder.AppendLine($"  grpc_url: {report.Metadata.GrpcUrl}");
        builder.AppendLine($"  grpc_stats_url: {report.Metadata.GrpcStatsUrl}");
        builder.AppendLine($"  zlink_endpoint: {report.Metadata.ZLinkEndpoint}");
        builder.AppendLine($"  zlink_stats_url: {report.Metadata.ZLinkStatsUrl}");
        builder.AppendLine($"  zlink_raw_endpoint: {report.Metadata.ZLinkRawEndpoint}");
        builder.AppendLine($"  zlink_raw_command_endpoint: {report.Metadata.ZLinkRawCommandEndpoint}");
        builder.AppendLine($"  zlink_raw_stats_url: {report.Metadata.ZLinkRawStatsUrl}");
        builder.AppendLine($"  result_json: {report.Metadata.ResultJson}");
        builder.AppendLine($"  report_txt: {report.Metadata.ReportText}");
    }

    builder.AppendLine();
    foreach (var group in report.Results.GroupBy(static result => result.Scenario))
    {
        builder.AppendLine($"  > Benchmarking current for {group.Key}...");
        builder.AppendLine("    Testing local:");
        builder.AppendLine("      | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | Client CPU | Client Mem | Server CPU | Server Mem |");
        builder.AppendLine("      |----------|------------------|--------------|--------------|--------------|--------------|------------|------------|------------|------------|");
        foreach (var result in group)
        {
            builder.AppendLine(
                $"      | {result.SizeText,-8} | {result.ThroughputText,16} | {result.BandwidthText,12} | {result.LatencyMeanText,12} | {result.LatencyP95Text,12} | {result.LatencyP99Text,12} | {result.ClientCpuText,10} | {result.ClientMemoryText,10} | {result.ServerCpuText,10} | {result.ServerMemoryText,10} |");
        }

        builder.AppendLine();
    }

    foreach (var result in report.Results)
    {
        foreach (var line in result.PerfLines)
        {
            builder.AppendLine(line);
        }
    }

    return builder.ToString();
}

static async ValueTask<BenchReportMetadata> CreateMetadataAsync(BenchOptions options)
{
    return new BenchReportMetadata(
        DateTimeOffset.UtcNow,
        CpuName(),
        RuntimeInformation.OSDescription,
        await RunShellCommandAsync("dotnet", "--version"),
        await RunShellCommandAsync("git", "rev-parse", "--short", "HEAD"),
        options.Configuration,
        options.PayloadSizes,
        options.RequestWindow,
        options.SendConcurrency,
        options.LatencySampleLimit,
        options.Warmup,
        options.DurationSeconds,
        options.CommandSettleMs,
        options.GrpcUrl,
        options.GrpcStatsUrl,
        options.ZLinkEndpoint,
        options.ZLinkStatsUrl,
        options.ZLinkRawEndpoint,
        options.ZLinkRawCommandEndpoint,
        options.ZLinkRawStatsUrl,
        Path.Combine(options.Output, "results.json"),
        options.ReportPath);
}

static async ValueTask<string> RunShellCommandAsync(string fileName, params string[] args)
{
    try
    {
        var startInfo = new ProcessStartInfo(fileName)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        foreach (var arg in args)
        {
            startInfo.ArgumentList.Add(arg);
        }

        using var process = Process.Start(startInfo);
        if (process is null)
        {
            return "unknown";
        }

        var output = await process.StandardOutput.ReadToEndAsync();
        await process.WaitForExitAsync();
        return process.ExitCode == 0 ? output.Trim() : "unknown";
    }
    catch
    {
        return "unknown";
    }
}

static string CpuName()
{
    const string cpuInfo = "/proc/cpuinfo";
    if (File.Exists(cpuInfo))
    {
        var model = File.ReadLines(cpuInfo)
            .FirstOrDefault(static line => line.StartsWith("model name", StringComparison.OrdinalIgnoreCase));
        if (model is not null)
        {
            var separator = model.IndexOf(':');
            if (separator >= 0)
            {
                return model[(separator + 1)..].Trim();
            }
        }
    }

    return RuntimeInformation.ProcessArchitecture.ToString();
}

internal sealed record BenchReport(
    BenchReportMetadata Metadata,
    IReadOnlyList<BenchResult> Results);

internal readonly record struct RequestSample(long ElapsedMicros, bool Success);

internal readonly record struct ResourceSample(TimeSpan CpuStart)
{
    public static ResourceSample Start()
    {
        return new ResourceSample(Process.GetCurrentProcess().TotalProcessorTime);
    }

    public double CpuSeconds()
    {
        return Math.Max(0, (Process.GetCurrentProcess().TotalProcessorTime - CpuStart).TotalSeconds);
    }

    public double WorkingSetMb()
    {
        return Process.GetCurrentProcess().WorkingSet64 / 1024.0 / 1024.0;
    }
}

internal sealed record BenchReportMetadata(
    DateTimeOffset GeneratedUtc,
    string Cpu,
    string Os,
    string DotNetSdk,
    string Commit,
    string Configuration,
    int[] PayloadSizes,
    int RequestWindow,
    int SendConcurrency,
    int LatencySampleLimit,
    int Warmup,
    int DurationSeconds,
    int CommandSettleMs,
    string GrpcUrl,
    string GrpcStatsUrl,
    string ZLinkEndpoint,
    string ZLinkStatsUrl,
    string ZLinkRawEndpoint,
    string ZLinkRawCommandEndpoint,
    string ZLinkRawStatsUrl,
    string ResultJson,
    string ReportText)
{
    public static BenchReportMetadata Empty { get; } = new(
        default,
        "",
        "",
	        "",
	        "",
	        "",
	        [],
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        "",
	        "",
	        "",
	        "",
	        "",
	        "",
	        "",
	        "",
	        "");

    public bool IsEmpty => GeneratedUtc == default;
}

internal sealed record BenchResult(
    string Scenario,
    string Unit,
    int PayloadSize,
    int DurationSeconds,
    long Completed,
    long Errors,
    long ServerErrors,
    int Warmup,
    double ElapsedSeconds,
    double Throughput,
    long P95Micros,
    long P99Micros,
    double MeanMicros,
    double? ServerMeanMicros,
    double? ServerP95Micros,
    double? ServerP99Micros,
    double ClientCpuSeconds,
    double ClientWorkingSetMb,
    double ServerCpuSeconds,
    double ServerWorkingSetMb)
{
    private double LatencyMeanMicros => ServerMeanMicros ?? MeanMicros;
    private double LatencyP95Micros => ServerP95Micros ?? P95Micros;
    private double LatencyP99Micros => ServerP99Micros ?? P99Micros;

    public string SizeText => $"{PayloadSize}B";
    public string ThroughputText => $"{Throughput / 1000.0:F2} {Unit}";
    public string BandwidthText => $"{Throughput * PayloadSize / 1_000_000.0:F2} MB/s";
    public string LatencyMeanText => $"{LatencyMeanMicros / 1000.0:F3} ms";
    public string LatencyP95Text => $"{LatencyP95Micros / 1000.0:F3} ms";
    public string LatencyP99Text => $"{LatencyP99Micros / 1000.0:F3} ms";
    public string ClientCpuText => $"{CpuPercent(ClientCpuSeconds):F1}%";
    public string ClientMemoryText => $"{ClientWorkingSetMb:F1} MB";
    public string ServerCpuText => $"{CpuPercent(ServerCpuSeconds):F1}%";
    public string ServerMemoryText => $"{ServerWorkingSetMb:F1} MB";
    public IEnumerable<string> PerfLines
    {
        get
        {
            yield return PerfLine("throughput", Throughput);
            yield return PerfLine("bandwidth", Throughput * PayloadSize / 1_000_000.0);
            yield return PerfLine("latency", LatencyMeanMicros / 1000.0);
            yield return PerfLine("latency_p95", LatencyP95Micros / 1000.0);
            yield return PerfLine("latency_p99", LatencyP99Micros / 1000.0);
            yield return PerfLine("client_cpu_percent", CpuPercent(ClientCpuSeconds));
            yield return PerfLine("client_memory_mb", ClientWorkingSetMb);
            yield return PerfLine("server_cpu_percent", CpuPercent(ServerCpuSeconds));
            yield return PerfLine("server_memory_mb", ServerWorkingSetMb);
        }
    }

    private double CpuPercent(double cpuSeconds)
    {
        return cpuSeconds / Math.Max(0.001, ElapsedSeconds) / Environment.ProcessorCount * 100.0;
    }

    private string PerfLine(string metric, double value)
    {
        return string.Join(',',
            "RESULT",
            "current",
            Scenario,
            "local",
            PayloadSize,
            metric,
            $"{value:F3}");
    }
}

internal sealed record BenchOptions(
    string Scenario,
	    int[] PayloadSizes,
	    int RequestWindow,
	    int SendConcurrency,
	    int LatencySampleLimit,
	    int Warmup,
    int DurationSeconds,
    int CommandSettleMs,
    string GrpcUrl,
    string ZLinkEndpoint,
    string GrpcStatsUrl,
    string ZLinkStatsUrl,
    string ZLinkRawEndpoint,
	    string ZLinkRawCommandEndpoint,
	    string ZLinkRawStatsUrl,
	    uint RunId,
	    string Output,
	    string ReportFile,
	    string Configuration,
	    TimeSpan Timeout)
{
    public string ReportPath =>
        Path.IsPathRooted(ReportFile)
            ? ReportFile
            : Path.Combine(Output, ReportFile);

    public static BenchOptions Parse(string[] args)
    {
        var reportStamp = DateTimeOffset.Now.ToString("yyyyMMdd_HHmmss");
        return new BenchOptions(
	            Value(args, "--scenario") ?? "all",
	            ParseInts(Value(args, "--payload-sizes") ?? "1024,4096"),
	            ParseInt(
	                Value(args, "--request-window"),
	                MaxParsedInt(Value(args, "--request-inflights"), 512)),
	            ParseInt(
	                Value(args, "--send-concurrency"),
	                MaxParsedInt(Value(args, "--command-inflights"), 8)),
	            ParseInt(Value(args, "--latency-sample-limit"), 200_000),
	            ParseInt(Value(args, "--warmup"), 1000),
            ParseInt(Value(args, "--duration-seconds") ?? Value(args, "--duration"), 5),
            ParseInt(Value(args, "--command-settle-ms"), 200),
            Value(args, "--grpc-url") ?? "http://127.0.0.1:5071",
            Value(args, "--zlink-endpoint") ?? "tcp://127.0.0.1:5072",
            Value(args, "--grpc-stats-url") ?? "http://127.0.0.1:5074",
            Value(args, "--zlink-stats-url") ?? "http://127.0.0.1:5073",
            Value(args, "--zlink-raw-endpoint") ?? "tcp://127.0.0.1:5075",
            Value(args, "--zlink-raw-command-endpoint") ?? "tcp://127.0.0.1:5077",
            Value(args, "--zlink-raw-stats-url") ?? "http://127.0.0.1:5076",
            (uint)Random.Shared.Next(1, int.MaxValue),
            Value(args, "--output") ?? "log/latest",
            Value(args, "--report-file") ?? $"with_grpc_dotnet_{reportStamp}.txt",
            Value(args, "--configuration") ?? Environment.GetEnvironmentVariable("CONFIGURATION") ?? "Release",
            TimeSpan.FromSeconds(ParseInt(Value(args, "--timeout-seconds"), 300)));
    }

    public void Validate()
    {
        if (PayloadSizes.Length == 0)
        {
            throw new InvalidOperationException("At least one payload size is required.");
        }

        if (RequestWindow <= 0)
        {
            throw new InvalidOperationException("RequestWindow must be positive.");
        }

        if (SendConcurrency <= 0)
        {
            throw new InvalidOperationException("SendConcurrency must be positive.");
        }

        if (LatencySampleLimit <= 0)
        {
            throw new InvalidOperationException("LatencySampleLimit must be positive.");
        }

        if (DurationSeconds <= 0)
        {
            throw new InvalidOperationException("DurationSeconds must be positive.");
        }

        foreach (var payloadSize in PayloadSizes)
        {
            if (payloadSize < BenchMetricHeaders.HeaderSize)
            {
                throw new InvalidOperationException(
                    $"Payload size must be at least {BenchMetricHeaders.HeaderSize} bytes.");
            }
        }

        ValidateHttpLoopback(GrpcUrl, nameof(GrpcUrl));
        ValidateHttpLoopback(GrpcStatsUrl, nameof(GrpcStatsUrl));
        ValidateHttpLoopback(ZLinkStatsUrl, nameof(ZLinkStatsUrl));
        ValidateHttpLoopback(ZLinkRawStatsUrl, nameof(ZLinkRawStatsUrl));
        ValidateTcpLoopback(ZLinkEndpoint, nameof(ZLinkEndpoint));
        ValidateTcpLoopback(ZLinkRawEndpoint, nameof(ZLinkRawEndpoint));
        ValidateTcpLoopback(ZLinkRawCommandEndpoint, nameof(ZLinkRawCommandEndpoint));
    }

    private static string? Value(string[] args, string name)
    {
        for (var i = 0; i < args.Length - 1; i++)
        {
            if (args[i] == name) return args[i + 1];
        }

        return null;
    }

    private static int ParseInt(string? value, int fallback)
    {
        return int.TryParse(value, out var parsed) ? parsed : fallback;
    }

    private static int[] ParseInts(string value)
    {
        return value.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(static item => int.Parse(item))
            .ToArray();
    }

    private static int MaxParsedInt(string? value, int fallback)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return fallback;
        }

        var parsed = ParseInts(value);
        return parsed.Length == 0 ? fallback : parsed.Max();
    }

    private static void ValidateHttpLoopback(string value, string name)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out var uri)
            || uri.Scheme != Uri.UriSchemeHttp
            || !IsLoopback(uri.Host))
        {
            throw new InvalidOperationException($"{name} must be an http loopback URL.");
        }
    }

    private static void ValidateTcpLoopback(string value, string name)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out var uri)
            || uri.Scheme != "tcp"
            || !IsLoopback(uri.Host))
        {
            throw new InvalidOperationException($"{name} must be a tcp loopback endpoint.");
        }
    }

    private static bool IsLoopback(string host)
    {
        if (host.Equals("localhost", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return IPAddress.TryParse(host, out var address) && IPAddress.IsLoopback(address);
    }
}

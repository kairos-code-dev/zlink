using System.Collections.Concurrent;
using System.Diagnostics;
using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using RegistrationCodec.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;

var options = ServerOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

var builder = WebApplication.CreateBuilder(args);
builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(console =>
{
    console.SingleLine = true;
    console.TimestampFormat = "HH:mm:ss.fff ";
});
builder.WebHost.UseUrls(options.HttpUrl);
builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));
builder.Services.AddSingleton<SingletonProbe>();
builder.Services.AddScoped<ScopedProbe>();
builder.Services.AddSingleton<FirstFilter>();
builder.Services.AddSingleton<SecondFilter>();

builder.Services.AddZLinkFramework(framework =>
{
    framework.ConfigureDispatch()
        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
        .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
        .TraceLabel(options.Rid);
    framework.Codecs.AddJson();
    if (options.CodecMode != "json-only")
    {
        framework.Codecs.Use(ZLinkProtobufCodec.Default);
        framework.Codecs.Use(ZLinkMessagePackCodec.Default);
    }
    framework.AddHandlersFromAssemblyOf<EchoAutoRequestHandler>();
    framework.UseFilter<FirstFilter>();
    framework.UseFilter<SecondFilter>();

    var channel = framework.AddClientServerChannel(RegistrationCodecNames.Channel)
        .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"))
        .EnableClient(Require(options.ChannelEndpoint, "--channel-endpoint"));
    channel.AddHandlerGroup("auto");
    channel.AddHandlerGroup("attr");
    channel.AddRequestHandler<EchoManualRequestHandler, EchoManualReq, EchoReply>("EchoManual");
    channel.AddSendHandler<EchoManualCommandHandler, EchoManualCommand>("EchoManualCommand");
    channel.AddRequestHandler<JsonEchoRequestHandler, JsonEchoReq, EchoReply>("EchoJson");
    channel.AddSendHandler<JsonEchoCommandHandler, JsonEchoCommand>("EchoJsonCommand");
    channel.AddRequestHandler<ProtobufEchoRequestHandler, StringValue, StringValue>("EchoProtobuf");
    channel.AddSendHandler<ProtobufEchoCommandHandler, StringValue>("EchoProtobufCommand");
    channel.AddRequestHandler<MessagePackEchoRequestHandler, PackedEchoReq, PackedEchoReq>("EchoMessagePack");
    channel.AddSendHandler<MessagePackEchoCommandHandler, PackedEchoCommand>("EchoMessagePackCommand");
    channel.AddRequestHandler<DiEchoRequestHandler, EchoReq, EchoReply>("EchoDi");

    if (options.InvalidMode == "duplicate")
    {
        channel.AddRequestHandler<DuplicateEchoRequestHandler, EchoManualReq, EchoReply>("EchoManual");
    }
});

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
{
    evidence.Clear();
    return Results.Ok(new { status = "cleared" });
});
app.MapPost("/scenario/rc-a1", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
{
    var reply = await channel.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoAutoReq("rc-a1"))
        .Async<EchoReply>(cancellationToken);
    await channel.SendToChannel(
            RegistrationCodecNames.Channel,
            new EchoAutoCommand("cmd-rc-a1", "rc-a1-send"))
        .Async(cancellationToken);
    return Results.Ok(reply);
});
app.MapPost("/scenario/rc-a2", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
{
    var reply = await channel.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoReq("rc-a2"))
        .PacketName("EchoAttr")
        .Async<EchoReply>(cancellationToken);
    await channel.SendToChannel(
            RegistrationCodecNames.Channel,
            new EchoCommand("cmd-rc-a2", "rc-a2-send"))
        .PacketName("EchoAttrCommand")
        .Async(cancellationToken);
    return Results.Ok(reply);
});
app.MapPost("/scenario/rc-a3", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
{
    var reply = await channel.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoManualReq("rc-a3"))
        .Async<EchoReply>(cancellationToken);
    await channel.SendToChannel(
            RegistrationCodecNames.Channel,
            new EchoManualCommand("cmd-rc-a3", "rc-a3-send"))
        .Async(cancellationToken);
    return Results.Ok(reply);
});
app.MapPost("/scenario/rc-a4-a5", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
{
    var first = await channel.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoReq("rc-a4-1"))
        .PacketName("EchoDi")
        .Async<EchoReply>(cancellationToken);
    var second = await channel.RequestToChannel(
            RegistrationCodecNames.Channel,
            new EchoReq("rc-a4-2"))
        .PacketName("EchoDi")
        .Async<EchoReply>(cancellationToken);
    return Results.Ok(new[] { first, second });
});
app.MapPost("/scenario/rc-b1-b4", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
{
    var json = await channel.RequestToChannel(
            RegistrationCodecNames.Channel,
            new JsonEchoReq("rc-b1"))
        .PacketName("EchoJson")
        .Async<EchoReply>(cancellationToken);
    await channel.SendToChannel(
            RegistrationCodecNames.Channel,
            new JsonEchoCommand("cmd-rc-b1", "rc-b1-send"))
        .PacketName("EchoJsonCommand")
        .Async(cancellationToken);

    var protobuf = await channel.RequestToChannel(
            RegistrationCodecNames.Channel,
            new StringValue { Value = "rc-b2" })
        .PacketName("EchoProtobuf")
        .Async<StringValue>(cancellationToken);
    await channel.SendToChannel(
            RegistrationCodecNames.Channel,
            new StringValue { Value = "rc-b2-send" })
        .PacketName("EchoProtobufCommand")
        .Async(cancellationToken);

    var packed = await channel.RequestToChannel(
            RegistrationCodecNames.Channel,
            new PackedEchoReq { Value = "rc-b3" })
        .PacketName("EchoMessagePack")
        .Async<PackedEchoReq>(cancellationToken);
    await channel.SendToChannel(
            RegistrationCodecNames.Channel,
            new PackedEchoCommand { CommandId = "cmd-rc-b3", Value = "rc-b3-send" })
        .PacketName("EchoMessagePackCommand")
        .Async(cancellationToken);

    return Results.Ok(new CodecScenarioResult(json, protobuf.Value, packed.Value));
});
app.MapPost("/scenario/rc-b5", async (CancellationToken cancellationToken) =>
{
    var serverProject = Require(options.ServerProject, "--server-project");
    var httpPort = PickPort();
    var channelPort = PickPort();
    var serverUrl = $"http://127.0.0.1:{httpPort}";
    var channelEndpoint = $"tcp://127.0.0.1:{channelPort}";
    var evidenceFile = Path.Combine(options.LogDir, "codec-mismatch.evidence.log");
    var stdout = Path.Combine(options.LogDir, "codec-mismatch.stdout.log");
    var stderr = Path.Combine(options.LogDir, "codec-mismatch.stderr.log");
    using var process = new Process();
    process.StartInfo = new ProcessStartInfo("dotnet")
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
    };
    process.StartInfo.ArgumentList.Add("run");
    process.StartInfo.ArgumentList.Add("--project");
    process.StartInfo.ArgumentList.Add(serverProject);
    process.StartInfo.ArgumentList.Add("--");
    process.StartInfo.ArgumentList.Add("--rid");
    process.StartInfo.ArgumentList.Add("codec-mismatch");
    process.StartInfo.ArgumentList.Add("--http-url");
    process.StartInfo.ArgumentList.Add(serverUrl);
    process.StartInfo.ArgumentList.Add("--channel-endpoint");
    process.StartInfo.ArgumentList.Add(channelEndpoint);
    process.StartInfo.ArgumentList.Add("--codec-mode");
    process.StartInfo.ArgumentList.Add("json-only");
    process.StartInfo.ArgumentList.Add("--evidence-file");
    process.StartInfo.ArgumentList.Add(evidenceFile);
    process.StartInfo.ArgumentList.Add("--log-dir");
    process.StartInfo.ArgumentList.Add(options.LogDir);
    process.Start();
    var copyOut = CopyToFileAsync(process.StandardOutput, stdout);
    var copyErr = CopyToFileAsync(process.StandardError, stderr);
    try
    {
        await WaitHealthAsync(serverUrl, process, cancellationToken);
        using var mismatchHost = CreateClientHost(channelEndpoint, options.LogDir, "codec-mismatch-client");
        await mismatchHost.StartAsync(cancellationToken);
        var mismatchClient = mismatchHost.Services.GetRequiredService<IZLinkChannelClient>();

        var failed = false;
        try
        {
            await mismatchClient.RequestToChannel(
                    RegistrationCodecNames.Channel,
                    new StringValue { Value = "rc-b5" })
                .PacketName("EchoProtobuf")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<StringValue>(cancellationToken);
        }
        catch (Exception)
        {
            failed = true;
        }

        if (!failed)
        {
            return Results.Problem("RC-B5 Protobuf request unexpectedly succeeded against a JSON-only peer.");
        }

        var json = await mismatchClient.RequestToChannel(
                RegistrationCodecNames.Channel,
                new JsonEchoReq("rc-b5-json"))
            .PacketName("EchoJson")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<EchoReply>(cancellationToken);
        if (json.Value != "echo:rc-b5-json")
        {
            return Results.Problem("RC-B5 JSON fallback request did not recover after mismatch.");
        }

        await mismatchHost.StopAsync(cancellationToken);
    }
    finally
    {
        if (!process.HasExited)
        {
            process.Kill(entireProcessTree: true);
        }

        await process.WaitForExitAsync(cancellationToken);
        await Task.WhenAll(copyOut, copyErr);
    }

    return Results.Ok(new { status = "passed" });
});
await app.RunAsync();

static string Require(string? value, string name)
{
    return string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{name} is required.")
        : value;
}

static IHost CreateClientHost(string channelEndpoint, string logDir, string nodeId)
{
    return Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            services.AddZLinkFramework(framework =>
            {
                framework.ConfigureDispatch()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(Path.Combine(logDir, $"{nodeId}-flow.log"))
                    .TraceLabel(nodeId);
                framework.Codecs.AddJson();
                framework.Codecs.Use(ZLinkProtobufCodec.Default);
                framework.Codecs.Use(ZLinkMessagePackCodec.Default);
                framework.AddClientServerChannel(RegistrationCodecNames.Channel)
                    .EnableClient(channelEndpoint);
            });
        })
        .Build();
}

static int PickPort()
{
    using var socket = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
    socket.Start();
    var port = ((System.Net.IPEndPoint)socket.LocalEndpoint).Port;
    socket.Stop();
    return port;
}

static async Task WaitHealthAsync(string serverUrl, Process process, CancellationToken cancellationToken)
{
    using var http = new HttpClient();
    for (var i = 0; i < 120; i++)
    {
        if (process.HasExited)
        {
            throw new InvalidOperationException($"RC-B5 mismatch server exited early: {process.ExitCode}.");
        }

        try
        {
            using var response = await http.GetAsync($"{serverUrl}/health", cancellationToken);
            if (response.IsSuccessStatusCode)
            {
                return;
            }
        }
        catch (HttpRequestException)
        {
        }

        await Task.Delay(250, cancellationToken);
    }

    throw new TimeoutException($"Timed out waiting for RC-B5 mismatch server at {serverUrl}.");
}

static async Task CopyToFileAsync(StreamReader reader, string path)
{
    await using var stream = File.Open(path, FileMode.Create, FileAccess.Write, FileShare.Read);
    await using var writer = new StreamWriter(stream);
    while (await reader.ReadLineAsync() is { } line)
    {
        await writer.WriteLineAsync(line);
        await writer.FlushAsync();
    }
}

[ZLinkHandlerGroup("auto")]
internal sealed class EchoAutoRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<EchoAutoReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(
        EchoAutoReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-request|variant=auto|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}

[ZLinkHandlerGroup("auto")]
internal sealed class EchoAutoCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<EchoAutoCommand>
{
    public ValueTask HandleAsync(
        EchoAutoCommand message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"echo-command|variant=auto|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

[ZLinkHandlerGroup("attr")]
internal sealed class AttributeHandlers(EvidenceStore evidence)
{
    [ZLinkRequest(PacketName = "EchoAttr")]
    public EchoReply Request(EchoReq request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-request|variant=attr|value={request.Value}|content={context.ContentType}");
        return new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>");
    }

    [ZLinkSend(PacketName = "EchoAttrCommand")]
    public ValueTask Send(EchoCommand message, ZLinkSendContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"echo-command|variant=attr|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EchoManualRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<EchoManualReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(
        EchoManualReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-request|variant=manual|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}

internal sealed class EchoManualCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<EchoManualCommand>
{
    public ValueTask HandleAsync(
        EchoManualCommand message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"echo-command|variant=manual|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class JsonEchoRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<JsonEchoReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(
        JsonEchoReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-request|codec=json|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}

internal sealed class JsonEchoCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<JsonEchoCommand>
{
    public ValueTask HandleAsync(
        JsonEchoCommand message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-command|codec=json|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ProtobufEchoRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<StringValue, StringValue>
{
    public ValueTask<StringValue> HandleAsync(
        StringValue request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-request|codec=protobuf|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new StringValue { Value = $"echo:{request.Value}|content:{context.ContentType}" });
    }
}

internal sealed class ProtobufEchoCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<StringValue>
{
    public ValueTask HandleAsync(
        StringValue message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-command|codec=protobuf|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class MessagePackEchoRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<PackedEchoReq, PackedEchoReq>
{
    public ValueTask<PackedEchoReq> HandleAsync(
        PackedEchoReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-request|codec=msgpack|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new PackedEchoReq { Value = $"echo:{request.Value}|content:{context.ContentType}" });
    }
}

internal sealed class MessagePackEchoCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<PackedEchoCommand>
{
    public ValueTask HandleAsync(
        PackedEchoCommand message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-command|codec=msgpack|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class DiEchoRequestHandler(
    EvidenceStore evidence,
    SingletonProbe singleton,
    ScopedProbe scoped)
    : IZLinkRequestHandler<EchoReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(
        EchoReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"di|value={request.Value}|singleton={singleton.Id}|scoped={scoped.Id}|disposed={ScopedProbe.DisposedCount}");
        return ValueTask.FromResult(new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}

internal sealed class DuplicateEchoRequestHandler
    : IZLinkRequestHandler<EchoManualReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(
        EchoManualReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new EchoReply(request.Value, "duplicate"));
    }
}

internal sealed class FirstFilter(EvidenceStore evidence) : IZLinkHandlerFilter
{
    public async ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerDelegate next,
        CancellationToken cancellationToken)
    {
        evidence.Add($"filter|name=first|phase=before|packet={invocation.PacketName}");
        var result = await next(cancellationToken);
        evidence.Add($"filter|name=first|phase=after|packet={invocation.PacketName}");
        return result;
    }
}

internal sealed class SecondFilter(EvidenceStore evidence) : IZLinkHandlerFilter
{
    public async ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerDelegate next,
        CancellationToken cancellationToken)
    {
        evidence.Add($"filter|name=second|phase=before|packet={invocation.PacketName}");
        var result = await next(cancellationToken);
        evidence.Add($"filter|name=second|phase=after|packet={invocation.PacketName}");
        return result;
    }
}

internal sealed class SingletonProbe
{
    public string Id { get; } = Guid.NewGuid().ToString("N");
}

internal sealed class ScopedProbe : IDisposable
{
    private static int _disposedCount;

    public string Id { get; } = Guid.NewGuid().ToString("N");

    public static int DisposedCount => Volatile.Read(ref _disposedCount);

    public void Dispose()
    {
        Interlocked.Increment(ref _disposedCount);
    }
}

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly string? _filePath;

    public EvidenceStore(string? filePath)
    {
        _filePath = filePath;
        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_filePath)!);
            File.WriteAllText(_filePath, string.Empty);
        }
    }

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        if (string.IsNullOrWhiteSpace(_filePath))
        {
            return;
        }

        lock (_fileGate)
        {
            File.AppendAllText(_filePath, entry + Environment.NewLine);
        }
    }

    public string[] Snapshot() => _entries.ToArray();

    public void Clear()
    {
        while (_entries.TryDequeue(out _))
        {
        }

        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            lock (_fileGate)
            {
                File.WriteAllText(_filePath, string.Empty);
            }
        }
    }
}

internal sealed record ServerOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string? ChannelEndpoint,
    string? EvidenceFile,
    string? InvalidMode,
    string CodecMode,
    string? ServerProject)
{
    public static ServerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string? Get(string name) => values.TryGetValue(name, out var bucket) ? bucket[^1] : null;
        return new ServerOptions(
            Rid: Get("--rid") ?? "reg-codec-node",
            HttpUrl: Get("--http-url") ?? "http://127.0.0.1:0",
            LogDir: Get("--log-dir") ?? "logs",
            ChannelEndpoint: Get("--channel-endpoint"),
            EvidenceFile: Get("--evidence-file"),
            InvalidMode: Get("--invalid-mode"),
            CodecMode: Get("--codec-mode") ?? "all",
            ServerProject: Get("--server-project"));
    }
}

internal sealed record CodecScenarioResult(EchoReply Json, string ProtobufValue, string MessagePackValue);

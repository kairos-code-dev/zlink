using System.Collections.Concurrent;
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
        .TraceNodeId(options.Rid);
    framework.Codecs.AddJson();
    framework.Codecs.Use(ZLinkProtobufCodec.Default);
    framework.Codecs.Use(ZLinkMessagePackCodec.Default);
    framework.AddHandlersFromAssemblyOf<EchoAutoRequestHandler>();
    framework.UseFilter<FirstFilter>();
    framework.UseFilter<SecondFilter>();

    var channel = framework.AddClientServerChannel(RegistrationCodecNames.Channel)
        .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"));
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
await app.RunAsync();

static string Require(string? value, string name)
{
    return string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{name} is required.")
        : value;
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
    string? InvalidMode)
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
            InvalidMode: Get("--invalid-mode"));
    }
}

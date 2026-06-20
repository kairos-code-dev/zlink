using System.Collections.Concurrent;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace Zlink.Framework.E2ETests.Channels;

[Collection(nameof(FrameworkTestsCollection))]
public sealed class ChannelDispatchErrorTests
{
    [Fact(DisplayName = "DERR-001 channel missing request replies error and reports observer")]
    public async Task Channel_Missing_Request_Replies_Error_And_Reports_Observer()
    {
        var endpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var evidence = new ChannelDispatchErrorEvidence();
        var logs = new ChannelDispatchLogSink();
        using var serverHost = BuildServer(endpoint, evidence, logs);
        using var clientHost = BuildClient(endpoint);

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        try
        {
            var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            var warmup = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "warmup" })
                    .PacketName("KnownReq")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<KnownDispatchReply>(),
                reply => reply.Value == "known:warmup");

            Assert.Equal("known:warmup", warmup.Value);

            var error = await Assert.ThrowsAsync<InvalidOperationException>(async () => await client
                .RequestToChannel("dispatch", new UnknownDispatchRequest { Value = "missing" })
                .PacketName("UnknownReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<KnownDispatchReply>());

            Assert.Contains("UnknownReq", error.Message, StringComparison.Ordinal);

            var observed = await evidence.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkDispatchErrorSurface.Channel, observed.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.Request, observed.MessageKind);
            Assert.Equal(ZLinkDispatchErrorReason.HandlerMissing, observed.Reason);
            Assert.Equal(ZLinkDispatchErrorAction.ReplyError, observed.Action);
            Assert.Equal("UnknownReq", observed.PacketName);
            Assert.Equal("dispatch", observed.ChannelName);

            Assert.Contains(logs.Messages, entry =>
                entry.Level == LogLevel.Error
                && entry.Message.Contains("dispatch-error", StringComparison.Ordinal)
                && entry.Message.Contains("reason=HandlerMissing", StringComparison.Ordinal)
                && entry.Message.Contains("action=ReplyError", StringComparison.Ordinal)
                && entry.Message.Contains("packetName=UnknownReq", StringComparison.Ordinal));

            var afterError = await client
                .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "after-error" })
                .PacketName("KnownReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<KnownDispatchReply>();

            Assert.Equal("known:after-error", afterError.Value);
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
        }
    }

    [Fact(DisplayName = "DERR-002 channel missing send reports observer and keeps request path alive")]
    public async Task Channel_Missing_Send_Reports_Observer_And_Keeps_Request_Path_Alive()
    {
        var endpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var evidence = new ChannelDispatchErrorEvidence();
        var logs = new ChannelDispatchLogSink();
        using var serverHost = BuildServer(endpoint, evidence, logs);
        using var clientHost = BuildClient(endpoint);

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        try
        {
            var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            var warmup = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "warmup" })
                    .PacketName("KnownReq")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<KnownDispatchReply>(),
                reply => reply.Value == "known:warmup");

            Assert.Equal("known:warmup", warmup.Value);

            await client
                .SendToChannel("dispatch", new UnknownDispatchCommand { Value = "missing-send" })
                .PacketName("UnknownCommand")
                .Async();

            var observed = await evidence.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkDispatchErrorSurface.Channel, observed.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.Send, observed.MessageKind);
            Assert.Equal(ZLinkDispatchErrorReason.HandlerMissing, observed.Reason);
            Assert.Equal(ZLinkDispatchErrorAction.Drop, observed.Action);
            Assert.Equal("UnknownCommand", observed.PacketName);
            Assert.Equal("dispatch", observed.ChannelName);

            Assert.Contains(logs.Messages, entry =>
                entry.Level == LogLevel.Error
                && entry.Message.Contains("dispatch-error", StringComparison.Ordinal)
                && entry.Message.Contains("reason=HandlerMissing", StringComparison.Ordinal)
                && entry.Message.Contains("action=Drop", StringComparison.Ordinal)
                && entry.Message.Contains("packetName=UnknownCommand", StringComparison.Ordinal));

            var afterError = await client
                .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "after-send-error" })
                .PacketName("KnownReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<KnownDispatchReply>();

            Assert.Equal("known:after-send-error", afterError.Value);
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
        }
    }

    [Fact(DisplayName = "DERR-006 channel payload decode failure replies error and reports observer")]
    public async Task Channel_Payload_Decode_Failure_Replies_Error_And_Reports_Observer()
    {
        var endpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var evidence = new ChannelDispatchErrorEvidence();
        var logs = new ChannelDispatchLogSink();
        DecodeProbeHandler.Reset();
        using var serverHost = BuildServer(endpoint, evidence, logs);
        using var clientHost = BuildClient(endpoint);

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        try
        {
            var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            var warmup = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "warmup" })
                    .PacketName("KnownReq")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<KnownDispatchReply>(),
                reply => reply.Value == "known:warmup");

            Assert.Equal("known:warmup", warmup.Value);

            await using var context = global::Systems.Zlink.Zlink.CreateContext();
            await using var dealer = context.CreateDealerSocket();
            dealer.Connect(endpoint);
            var encodedRequestParts = ZLinkEnvelopeCodec.EncodeParts(
                new ZLinkEnvelopeHeader(
                    ZLinkMessageKind.Request,
                    "dispatch",
                    "DecodeReq",
                    ZLinkEnvelopeCodec.DefaultContentType,
                    "derr-006-corr",
                    null,
                    null,
                    null,
                    null),
                new KnownDispatchRequest { Value = "will-be-replaced" },
                typeof(KnownDispatchRequest));
            encodedRequestParts[1].Dispose();
            var requestParts = new[] { encodedRequestParts[0], Message.From("{") };

            var reply = await dealer.Request()
                .Message(requestParts[0])
                .Message(requestParts[1])
                .Timeout(TimeSpan.FromSeconds(2))
                .Async();
            ZLinkMessageParts.DisposeAll(requestParts);
            try
            {
                var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
                Assert.Equal(ZLinkMessageKind.Error, replyHeader.Kind);
                Assert.Equal("ZLinkFrameworkException", replyHeader.ErrorCode);
                Assert.Contains("PayloadDecodeFailed", replyHeader.ErrorMessage, StringComparison.Ordinal);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(reply);
            }

            Assert.Equal(0, DecodeProbeHandler.InvocationCount);
            var observed = await evidence.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkDispatchErrorSurface.Channel, observed.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.Request, observed.MessageKind);
            Assert.Equal(ZLinkDispatchErrorReason.PayloadDecodeFailed, observed.Reason);
            Assert.Equal(ZLinkDispatchErrorAction.ReplyError, observed.Action);
            Assert.Equal("DecodeReq", observed.PacketName);
            Assert.Equal("dispatch", observed.ChannelName);
            Assert.IsType<ZLinkFrameworkException>(observed.Exception);

            Assert.Contains(logs.Messages, entry =>
                entry.Level == LogLevel.Error
                && entry.Message.Contains("PayloadDecodeFailed", StringComparison.Ordinal)
                && entry.Message.Contains("ReplyError", StringComparison.Ordinal)
                && entry.Message.Contains("DecodeReq", StringComparison.Ordinal));

            var afterError = await client
                .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "after-decode-error" })
                .PacketName("DecodeReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<KnownDispatchReply>();

            Assert.Equal("decode:after-decode-error", afterError.Value);
            Assert.Equal(1, DecodeProbeHandler.InvocationCount);
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
            DecodeProbeHandler.Reset();
        }
    }

    [Fact(DisplayName = "DERR-007 channel handler exception replies error and reports observer")]
    public async Task Channel_Handler_Exception_Replies_Error_And_Reports_Observer()
    {
        var endpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var evidence = new ChannelDispatchErrorEvidence();
        var logs = new ChannelDispatchLogSink();
        using var serverHost = BuildServer(endpoint, evidence, logs);
        using var clientHost = BuildClient(endpoint);

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        try
        {
            var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            var warmup = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "warmup" })
                    .PacketName("KnownReq")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<KnownDispatchReply>(),
                reply => reply.Value == "known:warmup");

            Assert.Equal("known:warmup", warmup.Value);

            var error = await Assert.ThrowsAsync<InvalidOperationException>(async () => await client
                .RequestToChannel("dispatch", new ThrowDispatchRequest { Value = "boom" })
                .PacketName("ThrowReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<KnownDispatchReply>());

            Assert.Contains("DERR-007 handler exception", error.Message, StringComparison.Ordinal);

            var observed = await evidence.WaitAsync(TimeSpan.FromSeconds(2));
            Assert.Equal(ZLinkDispatchErrorSurface.Channel, observed.Surface);
            Assert.Equal(ZLinkDispatchMessageKind.Request, observed.MessageKind);
            Assert.Equal(ZLinkDispatchErrorReason.HandlerException, observed.Reason);
            Assert.Equal(ZLinkDispatchErrorAction.ReplyError, observed.Action);
            Assert.Equal("ThrowReq", observed.PacketName);
            Assert.Equal("dispatch", observed.ChannelName);
            Assert.IsType<InvalidOperationException>(observed.Exception);

            Assert.Contains(logs.Messages, entry =>
                entry.Level == LogLevel.Error
                && entry.ExceptionType == typeof(InvalidOperationException).FullName
                && entry.Message.Contains("dispatch-error", StringComparison.Ordinal)
                && entry.Message.Contains("reason=HandlerException", StringComparison.Ordinal)
                && entry.Message.Contains("action=ReplyError", StringComparison.Ordinal)
                && entry.Message.Contains("packetName=ThrowReq", StringComparison.Ordinal));

            var afterError = await client
                .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "after-handler-error" })
                .PacketName("KnownReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<KnownDispatchReply>();

            Assert.Equal("known:after-handler-error", afterError.Value);
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
        }
    }

    [Fact(DisplayName = "DERR-009 channel dispatch errors are written to file log")]
    public async Task Channel_Dispatch_Errors_Are_Written_To_File_Log()
    {
        var endpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var logPath = Path.Combine(Path.GetTempPath(), $"zlink-derr-009-{Guid.NewGuid():N}.log");
        var evidence = new ChannelDispatchErrorEvidence();
        var logs = new ChannelDispatchLogSink(logPath);
        DecodeProbeHandler.Reset();
        using var serverHost = BuildServer(endpoint, evidence, logs);
        using var clientHost = BuildClient(endpoint);

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        try
        {
            var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            _ = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("dispatch", new KnownDispatchRequest { Value = "warmup" })
                    .PacketName("KnownReq")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<KnownDispatchReply>(),
                reply => reply.Value == "known:warmup");

            await Assert.ThrowsAsync<InvalidOperationException>(async () => await client
                .RequestToChannel("dispatch", new UnknownDispatchRequest { Value = "missing" })
                .PacketName("UnknownReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<KnownDispatchReply>());

            await using (var context = global::Systems.Zlink.Zlink.CreateContext())
            await using (var dealer = context.CreateDealerSocket())
            {
                dealer.Connect(endpoint);
                var encodedRequestParts = ZLinkEnvelopeCodec.EncodeParts(
                    new ZLinkEnvelopeHeader(
                        ZLinkMessageKind.Request,
                        "dispatch",
                        "DecodeReq",
                        ZLinkEnvelopeCodec.DefaultContentType,
                        "derr-009-decode-corr",
                        null,
                        null,
                        null,
                        null),
                    new KnownDispatchRequest { Value = "will-be-replaced" },
                    typeof(KnownDispatchRequest));
                encodedRequestParts[1].Dispose();
                var requestParts = new[] { encodedRequestParts[0], Message.From("{") };
                var reply = await dealer.Request()
                    .Message(requestParts[0])
                    .Message(requestParts[1])
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async();
                ZLinkMessageParts.DisposeAll(requestParts);
                ZLinkMessageParts.DisposeAll(reply);
            }

            await Assert.ThrowsAsync<InvalidOperationException>(async () => await client
                .RequestToChannel("dispatch", new ThrowDispatchRequest { Value = "boom" })
                .PacketName("ThrowReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<KnownDispatchReply>());

            var text = await WaitForLogTextAsync(
                logPath,
                content => CountOccurrences(content, "dispatch-error") >= 3,
                TimeSpan.FromSeconds(2));
            Assert.Contains("surface=Channel", text, StringComparison.Ordinal);
            Assert.Contains("messageKind=Request", text, StringComparison.Ordinal);
            Assert.Contains("reason=HandlerMissing", text, StringComparison.Ordinal);
            Assert.Contains("reason=PayloadDecodeFailed", text, StringComparison.Ordinal);
            Assert.Contains("reason=HandlerException", text, StringComparison.Ordinal);
            Assert.Contains("action=ReplyError", text, StringComparison.Ordinal);
            Assert.Contains("packetName=UnknownReq", text, StringComparison.Ordinal);
            Assert.Contains("packetName=DecodeReq", text, StringComparison.Ordinal);
            Assert.Contains("packetName=ThrowReq", text, StringComparison.Ordinal);
            Assert.Contains("channelName=dispatch", text, StringComparison.Ordinal);
            Assert.Contains("correlationId=derr-009-decode-corr", text, StringComparison.Ordinal);
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
            DecodeProbeHandler.Reset();
            if (File.Exists(logPath))
            {
                File.Delete(logPath);
            }
        }
    }

    private static async Task<string> WaitForLogTextAsync(
        string path,
        Func<string, bool> predicate,
        TimeSpan timeout)
    {
        using var timeoutCts = new CancellationTokenSource(timeout);
        while (!timeoutCts.IsCancellationRequested)
        {
            var text = File.Exists(path)
                ? await File.ReadAllTextAsync(path, timeoutCts.Token)
                : string.Empty;
            if (predicate(text))
            {
                return text;
            }
            await Task.Delay(25, timeoutCts.Token);
        }
        throw new TimeoutException($"Timed out waiting for dispatch error log file '{path}'.");
    }

    private static int CountOccurrences(string text, string value)
    {
        var count = 0;
        var index = 0;
        while ((index = text.IndexOf(value, index, StringComparison.Ordinal)) >= 0)
        {
            count++;
            index += value.Length;
        }
        return count;
    }

    private static IHost BuildServer(
        string endpoint,
        ChannelDispatchErrorEvidence evidence,
        ChannelDispatchLogSink logs)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Logging.ClearProviders();
        builder.Logging.AddProvider(new ChannelDispatchLogProvider(logs));
        builder.Services.AddSingleton(evidence);
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<ChannelDispatchErrorObserver>();
            var channel = options.AddClientServerChannel("dispatch");
            channel.EnableServer(endpoint);
            channel.AddRequestHandler<KnownDispatchHandler, KnownDispatchRequest, KnownDispatchReply>("KnownReq");
            channel.AddRequestHandler<DecodeProbeHandler, KnownDispatchRequest, KnownDispatchReply>("DecodeReq");
            channel.AddRequestHandler<ThrowDispatchHandler, ThrowDispatchRequest, KnownDispatchReply>("ThrowReq");
        });
        return builder.Build();
    }

    private static IHost BuildClient(string endpoint)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("dispatch");
            channel.EnableClient(endpoint);
        });
        return builder.Build();
    }
}

public sealed class ChannelDispatchErrorEvidence
{
    private readonly TaskCompletionSource<ZLinkMessageDispatchErrorEvent> _first =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public ConcurrentQueue<ZLinkMessageDispatchErrorEvent> Events { get; } = [];

    public void Record(ZLinkMessageDispatchErrorEvent error)
    {
        Events.Enqueue(error);
        _first.TrySetResult(error);
    }

    public async Task<ZLinkMessageDispatchErrorEvent> WaitAsync(TimeSpan timeout)
    {
        return await _first.Task.WaitAsync(timeout);
    }
}

public sealed class ChannelDispatchErrorObserver(
    ChannelDispatchErrorEvidence evidence,
    ILogger<ChannelDispatchErrorObserver> logger) : IZLinkMessageDispatchErrorObserver
{
    public ValueTask OnDispatchErrorAsync(
        ZLinkMessageDispatchErrorEvent error,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogError(
            error.Exception,
            "dispatch-error surface={Surface} messageKind={MessageKind} reason={Reason} action={Action} packetName={PacketName} channelName={ChannelName} spotRid={SpotRid} correlationId={CorrelationId}",
            error.Surface,
            error.MessageKind,
            error.Reason,
            error.Action,
            error.PacketName,
            error.ChannelName,
            error.SpotRid,
            error.CorrelationId);
        evidence.Record(error);
        return ValueTask.CompletedTask;
    }
}

public sealed class KnownDispatchRequest
{
    public string Value { get; init; } = string.Empty;
}

public sealed class UnknownDispatchRequest
{
    public string Value { get; init; } = string.Empty;
}

public sealed class UnknownDispatchCommand
{
    public string Value { get; init; } = string.Empty;
}

public sealed class ThrowDispatchRequest
{
    public string Value { get; init; } = string.Empty;
}

public sealed class KnownDispatchReply
{
    public string Value { get; init; } = string.Empty;
}

public sealed class KnownDispatchHandler : IZLinkRequestHandler<KnownDispatchRequest, KnownDispatchReply>
{
    public ValueTask<KnownDispatchReply> HandleAsync(
        KnownDispatchRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        return ValueTask.FromResult(new KnownDispatchReply { Value = $"known:{request.Value}" });
    }
}

public sealed class DecodeProbeHandler : IZLinkRequestHandler<KnownDispatchRequest, KnownDispatchReply>
{
    private static int s_invocations;

    public static int InvocationCount => Volatile.Read(ref s_invocations);

    public static void Reset()
    {
        Volatile.Write(ref s_invocations, 0);
    }

    public ValueTask<KnownDispatchReply> HandleAsync(
        KnownDispatchRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        Interlocked.Increment(ref s_invocations);
        return ValueTask.FromResult(new KnownDispatchReply { Value = $"decode:{request.Value}" });
    }
}

public sealed class ThrowDispatchHandler : IZLinkRequestHandler<ThrowDispatchRequest, KnownDispatchReply>
{
    public ValueTask<KnownDispatchReply> HandleAsync(
        ThrowDispatchRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = request;
        _ = context;
        _ = cancellationToken;
        throw new InvalidOperationException("DERR-007 handler exception");
    }
}

public sealed class ChannelDispatchLogSink(string? filePath = null)
{
    public ConcurrentQueue<ChannelDispatchLogEntry> Messages { get; } = [];

    private readonly object _fileLock = new();

    public void Record(ChannelDispatchLogEntry entry)
    {
        Messages.Enqueue(entry);
        if (filePath is null)
        {
            return;
        }

        lock (_fileLock)
        {
            File.AppendAllText(
                filePath,
                $"{entry.Level} {entry.Message} exceptionType={entry.ExceptionType}{Environment.NewLine}");
        }
    }
}

public sealed record ChannelDispatchLogEntry(LogLevel Level, string Message, string? ExceptionType);

public sealed class ChannelDispatchLogProvider(ChannelDispatchLogSink sink) : ILoggerProvider
{
    public ILogger CreateLogger(string categoryName)
    {
        _ = categoryName;
        return new ChannelDispatchLogger(sink);
    }

    public void Dispose()
    {
    }
}

public sealed class ChannelDispatchLogger(ChannelDispatchLogSink sink) : ILogger
{
    public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

    public bool IsEnabled(LogLevel logLevel) => true;

    public void Log<TState>(
        LogLevel logLevel,
        EventId eventId,
        TState state,
        Exception? exception,
        Func<TState, Exception?, string> formatter)
    {
        _ = eventId;
        sink.Record(new ChannelDispatchLogEntry(
            logLevel,
            formatter(state, exception),
            exception?.GetType().FullName));
    }
}

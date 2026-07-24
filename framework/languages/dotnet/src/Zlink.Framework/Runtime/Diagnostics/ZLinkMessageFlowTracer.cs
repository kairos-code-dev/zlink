using System.Globalization;
using System.Text;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Diagnostics;

// Success-path message-flow tracer — the twin of ZLinkDispatchErrorReporter for
// received/dispatched/replied/sent/reply_received/error transitions.
//
// PERFORMANCE: callers MUST guard event construction with Enabled(outcome) so that an
// "off" dispatch pays nothing but a volatile mode read (a C# lambda would heap-
// allocate a closure, so we use a call-site guard instead of a lazy delegate):
//     if (tracer.Enabled(outcome)) tracer.Trace(new ZLinkMessageFlowEvent(...));
internal sealed class ZLinkMessageFlowTracer
{
    internal const string LoggerCategory = "zlink.framework.dispatch";

    private readonly ILogger _logger;
    private readonly ZLinkDispatchOptionsModel _options;
    private readonly ZLinkMessageFlowObserverPump? _observerPump;
    private readonly ZLinkFrameworkRuntime? _runtime;
    private readonly IZLinkRuntimeErrorSink? _errorSink;

    public ZLinkMessageFlowTracer(
        ZLinkDispatchOptionsModel options,
        ILogger? logger = null,
        ZLinkFrameworkRuntime? runtime = null,
        ZLinkMessageFlowObserverPump? observerPump = null,
        IZLinkRuntimeErrorSink? errorSink = null)
    {
        _options = options;
        _runtime = runtime;
        _observerPump = observerPump;
        _errorSink = errorSink;
        _logger = logger ?? ZLinkStandardErrorLogger.Instance;
    }

    public bool CaptureEnabled =>
        _options.Diagnostics.EffectiveMessageFlow != ZLinkMessageFlowLogMode.Off
        || ObserverEnabled;

    // Cheap mode gate (relaxed/volatile read of the live mode). Build the event only
    // after this returns true.
    public bool Enabled(ZLinkMessageFlowOutcome outcome)
    {
        return ShouldLog(outcome) || ObserverEnabled;
    }

    public void Trace(ZLinkMessageFlowEvent flow)
    {
        var logEnabled = ShouldLog(flow.Outcome);
        var observerEnabled = ObserverEnabled;
        if (!logEnabled && !observerEnabled) return;

        flow = NormalizeFlowPair(flow);
        if (string.IsNullOrEmpty(flow.FlowId))
        {
            var current = ZLinkFlowContext.Current;
            if (current is { } value)
                flow = flow with { FlowId = value.FlowId, FlowOrigin = value.Origin };
        }

        // Sampling thins healthy traffic; dropped transitions always pass through.
        var logSampled = logEnabled
                         && (flow.Outcome is ZLinkMessageFlowOutcome.Dropped or ZLinkMessageFlowOutcome.Error
                             || Sample(ZLinkTraceFormat.FlowIdKey(flow) ?? string.Empty));
        if (!logSampled && !observerEnabled) return;

        if (logSampled)
        {
            try
            {
                LogDefault(flow);
            }
            catch (Exception ex)
            {
                ReportUnhandledCallbackException(ex);
            }
        }

        if (!observerEnabled) return;

        if (_runtime is not null)
            _runtime.TryEnqueueMessageFlowObserver(flow);
        else
            _observerPump?.Enqueue(flow);
    }

    private bool ObserverEnabled =>
        _options.MessageFlowObserver is not null || _options.MessageFlowObserverType is not null;

    private void ReportUnhandledCallbackException(Exception exception)
    {
        if (_errorSink is not null)
            _errorSink.ReportUnhandledCallbackException(exception);
        else
            _runtime?.TryReportUnhandledCallbackException(exception);
    }

    internal bool ShouldLog(ZLinkMessageFlowOutcome outcome) =>
        (int)_options.Diagnostics.EffectiveMessageFlow >= (int)RequiredMode(outcome);

    internal static ILogger CreateLogger(ILoggerFactory? factory, ILogger? fallback = null) =>
        factory?.CreateLogger(LoggerCategory) ?? fallback ?? ZLinkStandardErrorLogger.Instance;

    private static ZLinkMessageFlowLogMode RequiredMode(ZLinkMessageFlowOutcome outcome)
    {
        return outcome is ZLinkMessageFlowOutcome.Dropped or ZLinkMessageFlowOutcome.Error
            ? ZLinkMessageFlowLogMode.ErrorsOnly
            : ZLinkMessageFlowLogMode.KeyTransitions;
    }

    private bool Sample(string flowId)
    {
        var rate = _options.Diagnostics.SampleRate;
        if (rate >= 1.0d) return true;

        if (rate <= 0.0d) return false;

        const ulong offset = 14695981039346656037ul;
        const ulong prime = 1099511628211ul;
        var hash = offset;
        foreach (var character in flowId)
        {
            hash ^= (byte)character;
            hash *= prime;
        }

        return hash / (double)ulong.MaxValue < rate;
    }

    private void LogDefault(ZLinkMessageFlowEvent flow)
    {
        var diagnostics = _options.Diagnostics;
        long? size = flow.MessageSize is { } messageSize
                     && (int)diagnostics.EffectiveMessageFlow >= (int)ZLinkMessageFlowLogMode.Verbose
                     && diagnostics.IncludeMessageSizes
            ? messageSize
            : null;

        // Separated file (diagnostics.log_file) vs the shared app logger. Both carry
        // structured key/value fields (so collectors ingest without parsing).
        if (diagnostics.LogFile is { } path)
        {
            ZLinkTraceFileWriter.Write(
                path,
                ZLinkTraceFormat.FlowLine(flow, diagnostics.Label, size),
                _errorSink,
                _runtime);
            return;
        }

        var level = ZLinkTraceFormat.ResolveLogLevel(flow);
        if (!_logger.IsEnabled(level)) return;

        _logger.Log(
            level,
            "phase={Phase} surface={Surface} kind={Kind} label={Label} packet={Packet} channel={Channel} topic={Topic} corr={Corr} flow={Flow} origin={Origin} src={Src} localRid={LocalRid} peerRid={PeerRid} socket={Socket} spot={Spot} actor={Actor} errorReason={ErrorReason} errorAction={ErrorAction} errorType={ErrorType} errorMessage={ErrorMessage} size={Size}",
            ZLinkTraceFormat.OutcomeKey(flow.Outcome),
            flow.Surface,
            flow.MessageKind,
            diagnostics.Label,
            flow.PacketName,
            flow.ChannelName,
            flow.Topic,
            flow.CorrelationId,
            ZLinkTraceFormat.FlowIdKey(flow),
            ZLinkTraceFormat.FlowOriginKey(flow),
            flow.SourceRid,
            flow.LocalRid,
            flow.PeerRid,
            flow.SocketRole,
            flow.SpotId,
            flow.ActorId,
            flow.ErrorReason,
            flow.ErrorAction,
            flow.ErrorType,
            flow.ErrorMessage,
            size);
    }

    private static ZLinkMessageFlowEvent NormalizeFlowPair(ZLinkMessageFlowEvent flow)
    {
        var hasFlowId = !string.IsNullOrEmpty(flow.FlowId);
        var hasFlowOrigin = flow.FlowOrigin is not null;
        if (hasFlowId == hasFlowOrigin) return flow;
        return flow with { FlowId = string.Empty, FlowOrigin = null };
    }
}

internal sealed class ZLinkStandardErrorLogger : ILogger
{
    public static ZLinkStandardErrorLogger Instance { get; } = new();

    private ZLinkStandardErrorLogger()
    {
    }

    public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

    public bool IsEnabled(LogLevel logLevel) => logLevel != LogLevel.None;

    public void Log<TState>(
        LogLevel logLevel,
        EventId eventId,
        TState state,
        Exception? exception,
        Func<TState, Exception?, string> formatter)
    {
        if (!IsEnabled(logLevel)) return;
        Console.Error.WriteLine($"zlink flow: {formatter(state, exception)}");
    }
}

// Minimal thread-safe append writer for the dedicated tracing file, mirroring the
// C++ file sink (open-append per line). Creates parent directories.
internal static class ZLinkTraceFileWriter
{
    private static readonly object Gate = new();

    public static void Write(
        string path,
        string line,
        IZLinkRuntimeErrorSink? errorSink,
        ZLinkFrameworkRuntime? runtime)
    {
        try
        {
            var directory = Path.GetDirectoryName(path);
            lock (Gate)
            {
                if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);

                File.AppendAllText(path, line + Environment.NewLine);
            }
        }
        catch (Exception ex)
        {
            if (errorSink is not null)
                errorSink.ReportUnhandledCallbackException(ex);
            else
                runtime?.TryReportUnhandledCallbackException(ex);
        }
    }
}

// Shared formatter for the file/clog-style text lines (key=value, present fields
// only) used by both the flow tracer and the dispatch error reporter.
internal static class ZLinkTraceFormat
{
    public static string FlowLine(ZLinkMessageFlowEvent flow, string? label, long? size)
    {
        var builder = new StringBuilder("message flow");
        Append(builder, "phase", OutcomeKey(flow.Outcome));
        Append(builder, "surface", flow.Surface.ToString());
        Append(builder, "kind", flow.MessageKind.ToString());
        Append(builder, "label", label);
        Append(builder, "packet", flow.PacketName);
        Append(builder, "channel", flow.ChannelName);
        Append(builder, "topic", flow.Topic);
        Append(builder, "corr", flow.CorrelationId);
        Append(builder, "flow", FlowIdKey(flow));
        Append(builder, "origin", FlowOriginKey(flow));
        Append(builder, "src", flow.SourceRid);
        Append(builder, "localRid", flow.LocalRid);
        Append(builder, "peerRid", flow.PeerRid);
        Append(builder, "socket", flow.SocketRole);
        Append(builder, "spot", flow.SpotId);
        Append(builder, "actor", flow.ActorId);
        Append(builder, "errorReason", flow.ErrorReason?.ToString());
        Append(builder, "errorAction", flow.ErrorAction?.ToString());
        Append(builder, "errorType", flow.ErrorType);
        Append(builder, "errorMessage", flow.ErrorMessage);
        if (size is { } value) Append(builder, "size", value.ToString(CultureInfo.InvariantCulture));

        return $"{DateTime.Now:O} {LogLevelKey(flow)} zlink.framework.dispatch - {builder}";
    }

    public static LogLevel ResolveLogLevel(ZLinkMessageFlowEvent flow)
    {
        if (flow.ErrorReason == ZLinkDispatchErrorReason.HandlerException) return LogLevel.Error;
        if (flow.ErrorReason is ZLinkDispatchErrorReason.HandlerMissing
            or ZLinkDispatchErrorReason.PayloadDecodeFailed
            or ZLinkDispatchErrorReason.InvalidFrame)
            return flow.MessageKind == ZLinkDispatchMessageKind.Publish
                ? LogLevel.Debug
                : LogLevel.Warning;
        if (flow.Outcome == ZLinkMessageFlowOutcome.Error) return LogLevel.Error;
        if (flow.Outcome == ZLinkMessageFlowOutcome.Dropped)
            return flow.MessageKind == ZLinkDispatchMessageKind.Publish
                ? LogLevel.Debug
                : LogLevel.Warning;
        return LogLevel.Information;
    }

    private static string LogLevelKey(ZLinkMessageFlowEvent flow) =>
        ResolveLogLevel(flow).ToString().ToLowerInvariant();

    public static string OutcomeKey(ZLinkMessageFlowOutcome outcome)
    {
        return outcome switch
        {
            ZLinkMessageFlowOutcome.Received => "received",
            ZLinkMessageFlowOutcome.Dispatched => "dispatched",
            ZLinkMessageFlowOutcome.Replied => "replied",
            ZLinkMessageFlowOutcome.Dropped => "dropped",
            ZLinkMessageFlowOutcome.Sent => "sent",
            ZLinkMessageFlowOutcome.ReplyReceived => "reply-received",
            ZLinkMessageFlowOutcome.Error => "error",
            _ => outcome.ToString().ToLowerInvariant()
        };
    }

    public static string? FlowIdKey(ZLinkMessageFlowEvent flow) =>
        !string.IsNullOrEmpty(flow.FlowId) && flow.FlowOrigin is not null
            ? flow.FlowId
            : null;

    public static string? FlowOriginKey(ZLinkMessageFlowEvent flow) =>
        FlowIdKey(flow) is not null
            ? flow.FlowOrigin!.Value.ToString().ToLowerInvariant()
            : null;

    private static void Append(StringBuilder builder, string key, string? value)
    {
        if (!string.IsNullOrEmpty(value)) builder.Append(' ').Append(key).Append('=').Append(value);
    }
}

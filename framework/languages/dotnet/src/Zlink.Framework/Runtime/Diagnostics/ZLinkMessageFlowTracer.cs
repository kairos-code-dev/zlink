using System.Globalization;
using System.Text;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Zlink.Framework.Runtime.Diagnostics;

// Success-path message-flow tracer — the twin of ZLinkDispatchErrorReporter for
// received/dispatched/replied/sent/reply_received/error transitions, keyed by
// correlation id.
//
// PERFORMANCE: callers MUST guard event construction with Enabled(outcome) so that an
// "off" dispatch pays nothing but a volatile mode read (a C# lambda would heap-
// allocate a closure, so we use a call-site guard instead of a lazy delegate):
//     if (tracer.Enabled(outcome)) tracer.Trace(new ZLinkMessageFlowEvent(...));
internal sealed class ZLinkMessageFlowTracer
{
    private static long _tracedCount;
    private readonly ILogger _logger;
    private readonly ZLinkDispatchOptionsModel _options;
    private readonly IServiceProvider _services;
    private long _sampleCounter;

    public ZLinkMessageFlowTracer(
        ZLinkDispatchOptionsModel options,
        IServiceProvider services,
        ILogger? logger = null)
    {
        _options = options;
        _services = services;
        _logger = logger ?? NullLogger.Instance;
    }

    public static long TracedCount => Interlocked.Read(ref _tracedCount);

    // Cheap mode gate (relaxed/volatile read of the live mode). Build the event only
    // after this returns true.
    public bool Enabled(ZLinkMessageFlowOutcome outcome)
    {
        return (int)_options.Diagnostics.EffectiveMessageFlow >= (int)RequiredMode(outcome);
    }

    public void Trace(ZLinkMessageFlowEvent flow)
    {
        if (!Enabled(flow.Outcome)) return;

        // Sampling thins healthy traffic; dropped transitions always pass through.
        if (flow.Outcome != ZLinkMessageFlowOutcome.Dropped
            && flow.Outcome != ZLinkMessageFlowOutcome.Error
            && !Sample())
            return;

        Interlocked.Increment(ref _tracedCount);

        try
        {
            LogDefault(flow);
        }
        catch (Exception ex)
        {
            ZLinkRuntimeErrorSink.ReportUnhandledCallbackException(ex);
        }

        if (_options.MessageFlowObserver is null && _options.MessageFlowObserverType is null) return;

        _ = Task.Run(async () =>
        {
            try
            {
                var observer = ResolveObserver();
                if (observer is not null)
                    await observer.OnMessageFlowAsync(flow, CancellationToken.None)
                        .ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                ZLinkRuntimeErrorSink.ReportUnhandledCallbackException(ex);
            }
        });
    }

    private static ZLinkMessageFlowLogMode RequiredMode(ZLinkMessageFlowOutcome outcome)
    {
        return outcome is ZLinkMessageFlowOutcome.Dropped or ZLinkMessageFlowOutcome.Error
            ? ZLinkMessageFlowLogMode.ErrorsOnly
            : ZLinkMessageFlowLogMode.KeyTransitions;
    }

    private bool Sample()
    {
        var rate = _options.Diagnostics.SampleRate;
        if (rate >= 1.0d) return true;

        if (rate <= 0.0d) return false;

        var stride = (long)(1.0d / rate + 0.5d);
        if (stride < 1) stride = 1;

        return Interlocked.Increment(ref _sampleCounter) % stride == 0;
    }

    private IZLinkMessageFlowObserver? ResolveObserver()
    {
        if (_options.MessageFlowObserver is { } observer) return observer;

        if (_options.MessageFlowObserverType is not { } observerType) return null;

        return (IZLinkMessageFlowObserver)ActivatorUtilities.GetServiceOrCreateInstance(
            _services,
            observerType);
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
            ZLinkTraceFileWriter.Write(path, ZLinkTraceFormat.FlowLine(flow, diagnostics.Label, size));
            return;
        }

        if (!_logger.IsEnabled(LogLevel.Information)) return;

        _logger.LogInformation(
            "message flow outcome={Outcome} surface={Surface} kind={Kind} label={Label} packet={Packet} channel={Channel} topic={Topic} corr={Corr} src={Src} localRid={LocalRid} peerRid={PeerRid} socket={Socket} spot={Spot} actor={Actor} errorReason={ErrorReason} errorAction={ErrorAction} errorType={ErrorType} errorMessage={ErrorMessage} size={Size}",
            ZLinkTraceFormat.OutcomeKey(flow.Outcome),
            flow.Surface,
            flow.MessageKind,
            diagnostics.Label,
            flow.PacketName,
            flow.ChannelName,
            flow.Topic,
            flow.CorrelationId,
            flow.SourceRid,
            flow.LocalRid,
            flow.PeerRid,
            flow.SocketRole,
            flow.SpotRid,
            flow.ActorId,
            flow.ErrorReason,
            flow.ErrorAction,
            flow.ErrorType,
            flow.ErrorMessage,
            size);
    }
}

// Minimal thread-safe append writer for the dedicated tracing file, mirroring the
// C++ file sink (open-append per line). Creates parent directories.
internal static class ZLinkTraceFileWriter
{
    private static readonly object Gate = new();

    public static void Write(string path, string line)
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
            ZLinkRuntimeErrorSink.ReportUnhandledCallbackException(ex);
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
        Append(builder, "outcome", OutcomeKey(flow.Outcome));
        Append(builder, "surface", flow.Surface.ToString());
        Append(builder, "kind", flow.MessageKind.ToString());
        Append(builder, "label", label);
        Append(builder, "packet", flow.PacketName);
        Append(builder, "channel", flow.ChannelName);
        Append(builder, "topic", flow.Topic);
        Append(builder, "corr", flow.CorrelationId);
        Append(builder, "src", flow.SourceRid);
        Append(builder, "localRid", flow.LocalRid);
        Append(builder, "peerRid", flow.PeerRid);
        Append(builder, "socket", flow.SocketRole);
        Append(builder, "spot", flow.SpotRid);
        Append(builder, "actor", flow.ActorId);
        Append(builder, "errorReason", flow.ErrorReason?.ToString());
        Append(builder, "errorAction", flow.ErrorAction?.ToString());
        Append(builder, "errorType", flow.ErrorType);
        Append(builder, "errorMessage", flow.ErrorMessage);
        if (size is { } value) Append(builder, "size", value.ToString(CultureInfo.InvariantCulture));

        return $"{DateTime.Now:O} info zlink.framework.dispatch - {builder}";
    }

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

    private static void Append(StringBuilder builder, string key, string? value)
    {
        if (!string.IsNullOrEmpty(value)) builder.Append(' ').Append(key).Append('=').Append(value);
    }
}
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Zlink.Framework.Runtime.Diagnostics;

internal sealed class ZLinkDispatchErrorReporter(
    ZLinkDispatchOptionsModel options,
    IServiceProvider services,
    ILogger? logger = null)
{
    private static long _reportedCount;
    private readonly ILogger _logger = logger ?? NullLogger.Instance;

    public static long ReportedCount => Interlocked.Read(ref _reportedCount);

    public void Report(ZLinkMessageDispatchErrorEvent error)
    {
        Interlocked.Increment(ref _reportedCount);

        // off silences the default error log; every other mode keeps reporting
        // errors (errors_only is the default). A registered observer still fires.
        if (options.Diagnostics.EffectiveMessageFlow != ZLinkMessageFlowLogMode.Off)
        {
            LogDefault(error);
        }

        _ = Task.Run(async () =>
        {
            try
            {
                var observer = ResolveObserver();
                if (observer is null)
                {
                    return;
                }

                await observer.OnDispatchErrorAsync(error, CancellationToken.None)
                    .ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                ZLinkRuntimeErrorSink.ReportUnhandledCallbackException(ex);
            }
        });
    }

    private IZLinkMessageDispatchErrorObserver? ResolveObserver()
    {
        if (options.MessageDispatchErrorObserver is { } observer)
        {
            return observer;
        }

        if (options.MessageDispatchErrorObserverType is not { } observerType)
        {
            return null;
        }

        return (IZLinkMessageDispatchErrorObserver)ActivatorUtilities.GetServiceOrCreateInstance(
            services,
            observerType);
    }

    private void LogDefault(ZLinkMessageDispatchErrorEvent error)
    {
        var diagnostics = options.Diagnostics;

        // Separated tracing file vs the shared app logger (same choice as the flow
        // tracer), so success and failure read as one correlation-id-keyed stream.
        if (diagnostics.LogFile is { } path)
        {
            ZLinkTraceFileWriter.Write(path, ZLinkTraceFormat.ErrorLine(error, diagnostics.NodeId));
            return;
        }

        var level = error.Action == ZLinkDispatchErrorAction.ReplyError
            ? LogLevel.Error
            : LogLevel.Warning;
        if (!_logger.IsEnabled(level))
        {
            return;
        }

        // Format unified with the flow tracer: include corr/topic/src/actor/node so
        // a single grep on corr follows a message whether it succeeded or failed.
        _logger.Log(
            level,
            error.Exception,
            "ZLink message dispatch error {Surface} {Kind} {Reason} {Action} node={Node} {PacketName} {ChannelName} {Topic} corr={Corr} src={Src} {SpotRid} {ActorId}",
            error.Surface,
            error.MessageKind,
            error.Reason,
            error.Action,
            diagnostics.NodeId,
            error.PacketName,
            error.ChannelName,
            error.Topic,
            error.CorrelationId,
            error.SourceRid,
            error.SpotRid,
            error.ActorId);
    }
}

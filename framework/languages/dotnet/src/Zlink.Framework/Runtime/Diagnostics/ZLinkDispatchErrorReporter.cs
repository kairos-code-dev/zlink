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
        LogDefault(error);

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
        var level = error.Action == ZLinkDispatchErrorAction.ReplyError
            ? LogLevel.Error
            : LogLevel.Warning;
        if (!_logger.IsEnabled(level))
        {
            return;
        }

        _logger.Log(
            level,
            error.Exception,
            "ZLink message dispatch error {Surface} {Kind} {Reason} {Action} {PacketName} {ChannelName} {SpotRid} {ActorId}",
            error.Surface,
            error.MessageKind,
            error.Reason,
            error.Action,
            error.PacketName,
            error.ChannelName,
            error.SpotRid,
            error.ActorId);
    }
}

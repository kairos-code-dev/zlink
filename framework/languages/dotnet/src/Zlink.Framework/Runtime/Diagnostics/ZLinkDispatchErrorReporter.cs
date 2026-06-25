using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Diagnostics;

internal sealed class ZLinkDispatchErrorReporter(
    ZLinkDispatchOptionsModel options,
    IServiceProvider services,
    ILogger? logger = null)
{
    private static long _reportedCount;

    // Success-path tracer companion: every surface already receives a reporter, so
    // exposing the flow tracer here wires all dispatch sites without threading a new
    // parameter. Shares the same options (live mode), services and logger.
    public ZLinkMessageFlowTracer Flow { get; } = new(options, services, logger);

    public static long ReportedCount => Interlocked.Read(ref _reportedCount);

    public void Report(ZLinkDispatchFailure error)
    {
        Interlocked.Increment(ref _reportedCount);

        if (Flow.Enabled(ZLinkMessageFlowOutcome.Error))
        {
            Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Error,
                error.Surface,
                error.MessageKind,
                PacketName: error.PacketName,
                ChannelName: error.ChannelName,
                Topic: error.Topic,
                CorrelationId: error.CorrelationId,
                SourceRid: error.SourceRid,
                SpotRid: error.SpotRid,
                ActorId: error.ActorId,
                ErrorReason: error.Reason,
                ErrorAction: error.Action,
                ErrorType: error.Exception?.GetType().Name,
                ErrorMessage: error.Exception?.Message));
        }
    }
}

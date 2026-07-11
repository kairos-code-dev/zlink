using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Diagnostics;

internal sealed class ZLinkDispatchErrorReporter(
    ZLinkDispatchOptionsModel options,
    ILogger? logger = null,
    ZLinkFrameworkRuntime? runtime = null,
    ZLinkMessageFlowObserverPump? observerPump = null)
{
    private static long _reportedCount;

    // Success-path tracer companion: every surface already receives a reporter, so
    // exposing the flow tracer here wires all dispatch sites without threading a new
    // parameter. It shares the live options, logger, and generation-owned observer pump.
    public ZLinkMessageFlowTracer Flow { get; } = new(options, logger, runtime, observerPump);

    public static long ReportedCount => Interlocked.Read(ref _reportedCount);

    public void Report(ZLinkDispatchFailure error)
    {
        Interlocked.Increment(ref _reportedCount);

        if (Flow.Enabled(ZLinkMessageFlowOutcome.Error))
            Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Error,
                error.Surface,
                error.MessageKind,
                error.PacketName,
                error.ChannelName,
                error.Topic,
                error.CorrelationId,
                error.SourceRid,
                SpotRid: error.SpotRid,
                ActorId: error.ActorId,
                ErrorReason: error.Reason,
                ErrorAction: error.Action,
                ErrorType: error.Exception?.GetType().Name,
                ErrorMessage: error.Exception?.Message));
    }
}

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotTimerFailureEventFactory
{
    public static ZLinkSpotEvent Create(
        string sourceName,
        RoutingId spotRid,
        bool isEntrySpot,
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick,
        Exception exception,
        bool stopped)
    {
        return new ZLinkSpotEvent(
            sourceName,
            DateTimeOffset.UtcNow,
            stopped
                ? ZLinkSpotEventKind.TimerStoppedAfterUnhandledException
                : ZLinkSpotEventKind.TimerHandlerFailed,
            Status: null,
            Peers: null,
            Subjects: null,
            new ZLinkSpotTimerDiagnostic(
                spotRid,
                isEntrySpot,
                descriptor.Name,
                descriptor.HandlerType.FullName ?? descriptor.HandlerType.Name,
                tick.DeliveryIndex,
                tick.ScheduledIndex,
                exception.GetType().FullName ?? exception.GetType().Name,
                exception.Message));
    }
}

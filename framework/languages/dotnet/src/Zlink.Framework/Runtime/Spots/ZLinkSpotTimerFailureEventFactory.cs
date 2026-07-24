namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotTimerFailureEventFactory
{
    public static ZLinkSpotEvent Create(
        string sourceName,
        string spotId,
        bool isEntrySpot,
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick,
        Exception exception,
        bool stopped)
    {
        var diagnostic = new ZLinkSpotTimerDiagnostic(
                spotId,
                isEntrySpot,
                descriptor.Name,
                descriptor.HandlerType.FullName ?? descriptor.HandlerType.Name,
                tick.DeliveryIndex,
                tick.ScheduledIndex,
                exception.GetType().FullName ?? exception.GetType().Name,
                exception.Message);
        return stopped
            ? new ZLinkSpotEvent.TimerStoppedAfterUnhandledException(
                sourceName,
                DateTimeOffset.UtcNow,
                diagnostic)
            : new ZLinkSpotEvent.TimerHandlerFailed(
                sourceName,
                DateTimeOffset.UtcNow,
                diagnostic);
    }
}

namespace Zlink.Framework.AspNetCore.Monitoring;

internal static class ZLinkMonitoringEventMapper
{
    public static ZLinkSocketEvent? MapSocketEvent(
        ZLinkSocketMonitoringRegistration source,
        ZLinkBackendSocketMonitorEvent monitorEvent)
    {
        var eventKind = monitorEvent.NativeEvent switch
        {
            ZLinkSocketNativeEventType.Connected => ZLinkSocketEventKind.Connected,
            ZLinkSocketNativeEventType.ConnectionReady => ZLinkSocketEventKind.ConnectionReady,
            ZLinkSocketNativeEventType.Disconnected => ZLinkSocketEventKind.Disconnected,
            ZLinkSocketNativeEventType.HandshakeFailedNoDetail => ZLinkSocketEventKind.HandshakeFailed,
            ZLinkSocketNativeEventType.HandshakeFailedProtocol => ZLinkSocketEventKind.HandshakeFailed,
            ZLinkSocketNativeEventType.HandshakeFailedAuth => ZLinkSocketEventKind.HandshakeFailed,
            ZLinkSocketNativeEventType.PeerAdmissionChanged => ZLinkSocketEventKind.PeerAdmissionChanged,
            ZLinkSocketNativeEventType.Closed => ZLinkSocketEventKind.Closed,
            ZLinkSocketNativeEventType.CloseFailed => ZLinkSocketEventKind.Closed,
            ZLinkSocketNativeEventType.MonitorStopped => ZLinkSocketEventKind.Closed,
            _ => (ZLinkSocketEventKind?)null
        };

        if (eventKind is null) return null;
        if (source.Events.Count > 0 && !source.Events.Contains(eventKind.Value)) return null;

        return new ZLinkSocketEvent(
            source.SourceName,
            DateTimeOffset.UtcNow,
            eventKind.Value,
            monitorEvent.RoutingId,
            monitorEvent.LocalAddr,
            monitorEvent.RemoteAddr);
    }
}

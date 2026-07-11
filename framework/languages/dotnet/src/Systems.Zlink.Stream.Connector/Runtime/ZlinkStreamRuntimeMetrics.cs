using System.Diagnostics;
using System.Diagnostics.Metrics;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal static class ZlinkStreamRuntimeMetrics
{
    // Every record method contains the listener boundary. Metrics callbacks
    // are external observation code and must never escape into connector flow.
    internal const string MeterName = "zlink.framework";

    private static readonly Meter Meter = new(MeterName);

    private static readonly Counter<long> Reconnects =
        Meter.CreateCounter<long>("zlink.stream.reconnects", "{event}");

    private static readonly Histogram<double> HandshakeDuration =
        Meter.CreateHistogram<double>("zlink.stream.handshake.duration", "s");

    private static readonly Counter<long> HandshakeFailures =
        Meter.CreateCounter<long>("zlink.stream.handshake.failures", "{failure}");

    private static readonly Counter<long> InboundBytes =
        Meter.CreateCounter<long>("zlink.stream.inbound.bytes", "By");

    private static readonly Counter<long> OutboundBytes =
        Meter.CreateCounter<long>("zlink.stream.outbound.bytes", "By");

    internal static void RecordReconnectAttempt()
    {
        try
        {
            if (Reconnects.Enabled) Reconnects.Add(1);
        }
        catch
        {
        }
    }

    internal static HandshakeMeasurement BeginHandshake()
    {
        try
        {
            return HandshakeDuration.Enabled
                ? new HandshakeMeasurement(true, Stopwatch.GetTimestamp())
                : default;
        }
        catch
        {
            return default;
        }
    }

    internal static void RecordHandshakeCompleted(
        HandshakeMeasurement measurement,
        string transport)
    {
        if (!measurement.Enabled) return;

        var elapsedSeconds = Stopwatch.GetElapsedTime(measurement.StartedTimestamp).TotalSeconds;
        try
        {
            HandshakeDuration.Record(
                elapsedSeconds,
                new KeyValuePair<string, object?>("transport", transport));
        }
        catch
        {
        }
    }

    internal static void RecordHandshakeFailure(string transport, Exception exception)
    {
        var reason = exception switch
        {
            OperationCanceledException => "canceled",
            System.Security.Authentication.AuthenticationException => "authentication_error",
            _ => "transport_error"
        };

        try
        {
            if (!HandshakeFailures.Enabled) return;
            HandshakeFailures.Add(
                1,
                new KeyValuePair<string, object?>("transport", transport),
                new KeyValuePair<string, object?>("reason", reason));
        }
        catch
        {
        }
    }

    internal static void RecordInboundBytes(long bytes, string transport)
    {
        try
        {
            if (!InboundBytes.Enabled) return;
            InboundBytes.Add(
                bytes,
                new KeyValuePair<string, object?>("transport", transport));
        }
        catch
        {
        }
    }

    internal static void RecordOutboundBytes(long bytes, string transport)
    {
        try
        {
            if (!OutboundBytes.Enabled) return;
            OutboundBytes.Add(
                bytes,
                new KeyValuePair<string, object?>("transport", transport));
        }
        catch
        {
        }
    }

    internal static string TransportLabel(ZlinkStreamConnectorOptions options)
    {
        return options.Endpoint.Scheme switch
        {
            "tcp" => "tcp",
            "tls" => "tls",
            "ws" => "ws",
            "wss" => "wss",
            _ => "unknown"
        };
    }

    internal readonly record struct HandshakeMeasurement(bool Enabled, long StartedTimestamp);
}

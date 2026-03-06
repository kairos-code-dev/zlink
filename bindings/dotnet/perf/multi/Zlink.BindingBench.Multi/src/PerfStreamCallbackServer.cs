using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfStreamCallbackServer
{
    internal static int Run(string transport, int size)
    {
        const string pattern = "STREAM_CALLBACK";
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int settleSeconds = (settleMs + 999) / 1000;
        string endpoint = MultiEndpointFor(transport, "multi-stream-callback");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Stream);
        ApplyMultiSocketOptions(server, pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);

        int stopRequested = 0;
        int callbackFailed = 0;
        long payloadSeen = 0;
        long lastActivityTicks = Stopwatch.GetTimestamp();
        var stopParsers = new Dictionary<uint, Len32StopTokenParser>();
        var stopParsersLock = new object();

        StreamPacketHandler rawHandler = (rid, payload) =>
        {
            ReadOnlySpan<byte> payloadBytes = payload.AsReadOnlySpan();
            if (payloadBytes.Length == 1
                && (payloadBytes[0] == 0x00 || payloadBytes[0] == 0x01))
            {
                payload.Dispose();
                return 0;
            }

            if (payloadBytes.SequenceEqual(MultiStopToken)
                || ConsumeLen32StopToken(stopParsers, stopParsersLock, rid,
                    payloadBytes))
            {
                Interlocked.Exchange(ref stopRequested, 1);
                payload.Dispose();
                return 0;
            }

            Interlocked.Increment(ref payloadSeen);
            Interlocked.Exchange(ref lastActivityTicks,
                Stopwatch.GetTimestamp());

            try
            {
                server.StreamSend(rid, payload, SendFlags.None);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno))
            {
                payload.Dispose();
                Console.Error.WriteLine($"multi_server_error:stream_callback_send:{ex.Errno}");
                Interlocked.Exchange(ref callbackFailed, 1);
                Interlocked.Exchange(ref stopRequested, 1);
            }
            catch
            {
                payload.Dispose();
                Interlocked.Exchange(ref callbackFailed, 1);
                Interlocked.Exchange(ref stopRequested, 1);
            }
            return 0;
        };

        server.AttachStreamRaw(rawHandler);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");
        Thread.Sleep(200);

        long firstPacketDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(8, warmupSeconds + durationSeconds + settleSeconds + 5)
            * Stopwatch.Frequency;
        long idleBreakTicks = (long)(Stopwatch.Frequency
            * (Math.Max(rcvTimeoutMs * 2, 1000) / 1000.0));
        while (Volatile.Read(ref stopRequested) == 0)
        {
            if (Volatile.Read(ref callbackFailed) != 0)
                return 2;
            long currentPayloadSeen = Volatile.Read(ref payloadSeen);
            long nowTicks = Stopwatch.GetTimestamp();
            if (currentPayloadSeen > 0)
            {
                long idleTicks = nowTicks - Volatile.Read(ref lastActivityTicks);
                if (idleTicks >= idleBreakTicks)
                    break;
            }
            else if (nowTicks >= firstPacketDeadlineTicks)
            {
                break;
            }
            Thread.Sleep(1);
        }

        try
        {
            server.DetachStream();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
        }

        return Volatile.Read(ref callbackFailed) == 0 ? 0 : 2;
    }

    private static bool ConsumeLen32StopToken(
        Dictionary<uint, Len32StopTokenParser> stopParsers, object sync,
        uint routingId, ReadOnlySpan<byte> payload)
    {
        lock (sync)
        {
            if (!stopParsers.TryGetValue(routingId, out Len32StopTokenParser? parser))
            {
                parser = new Len32StopTokenParser();
                stopParsers[routingId] = parser;
            }
            return parser.Consume(payload);
        }
    }
}

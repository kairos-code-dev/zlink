using System;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfStreamLen32BeServer
{
    internal static int Run(string transport, int size)
    {
        const string pattern = "STREAM_LEN32BE";
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int settleSeconds = (settleMs + 999) / 1000;
        string endpoint = MultiEndpointFor(transport, "multi-stream-len32be");

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

        StreamBatchHandler len32BeHandler = (rid, messages) =>
        {
            for (int i = 0; i < messages.Length; i++)
            {
                Message message = messages[i];
                ReadOnlySpan<byte> payload = message.AsReadOnlySpan();
                if (payload.Length == 1
                    && (payload[0] == 0x00 || payload[0] == 0x01))
                {
                    message.Dispose();
                    continue;
                }

                if (payload.SequenceEqual(MultiStopToken))
                {
                    Interlocked.Exchange(ref stopRequested, 1);
                    message.Dispose();
                    continue;
                }

                Interlocked.Increment(ref payloadSeen);
                Interlocked.Exchange(ref lastActivityTicks,
                    Stopwatch.GetTimestamp());

                try
                {
                    server.StreamSend(rid, message, SendFlags.None);
                }
                catch (ZlinkException ex)
                {
                    message.Dispose();
                    Console.Error.WriteLine($"multi_server_error:stream_len32be_send:{ex.Errno}");
                    Interlocked.Exchange(ref callbackFailed, 1);
                    Interlocked.Exchange(ref stopRequested, 1);
                }
            }
            return 0;
        };

        server.AttachStreamLen32Be(len32BeHandler);
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
}

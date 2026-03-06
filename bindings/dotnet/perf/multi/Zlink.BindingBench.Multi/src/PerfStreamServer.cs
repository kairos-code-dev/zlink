using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfStreamServer
{
    internal static int Run(string transport, int size)
    {
        const string pattern = "STREAM";
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int settleSeconds = (settleMs + 999) / 1000;
        string endpoint = MultiEndpointFor(transport, "multi-stream");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Stream);
        ApplyMultiSocketOptions(server, pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");
        Thread.Sleep(200);

        var routingId = new byte[4096];
        var payload = new byte[Math.Max(4096,
            Math.Max(size + 512, MultiStopToken.Length + 64))];
        var stopParsers = new Dictionary<string, Len32StopTokenParser>(
            StringComparer.Ordinal);
        long payloadSeen = 0;
        long lastActivityTicks = Stopwatch.GetTimestamp();
        long firstPacketDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(8, warmupSeconds + durationSeconds + settleSeconds + 5)
            * Stopwatch.Frequency;
        long idleBreakTicks = (long)(Stopwatch.Frequency
            * (Math.Max(rcvTimeoutMs * 2, 1000) / 1000.0));

        while (true)
        {
            if (!TryReceiveStreamFrame(server, routingId.AsSpan(),
                    out int ridLen)
                || ridLen <= 0)
            {
                long nowTicks = Stopwatch.GetTimestamp();
                if (payloadSeen > 0 && nowTicks - lastActivityTicks >= idleBreakTicks)
                    break;
                if (payloadSeen == 0 && nowTicks >= firstPacketDeadlineTicks)
                    break;
                continue;
            }
            if (!TryReceiveStreamFrame(server, payload.AsSpan(), out int n)
                || n <= 0)
            {
                long nowTicks = Stopwatch.GetTimestamp();
                if (payloadSeen > 0 && nowTicks - lastActivityTicks >= idleBreakTicks)
                    break;
                if (payloadSeen == 0 && nowTicks >= firstPacketDeadlineTicks)
                    break;
                continue;
            }

            ReadOnlySpan<byte> body = payload.AsSpan(0, n);
            if (body.Length == 1 && (body[0] == 0x00 || body[0] == 0x01))
                continue;
            payloadSeen++;
            lastActivityTicks = Stopwatch.GetTimestamp();
            string routingKey = Convert.ToHexString(routingId.AsSpan(0, ridLen));
            if (!stopParsers.TryGetValue(routingKey, out Len32StopTokenParser? parser))
            {
                parser = new Len32StopTokenParser();
                stopParsers[routingKey] = parser;
            }
            if (IsStopTokenPayload(body) || parser.Consume(body))
                break;

            int sentRid = SendBlocking(server, routingId.AsSpan(0, ridLen),
                SendFlags.SendMore);
            if (sentRid <= 0)
                continue;

            _ = SendBlocking(server, body, SendFlags.None);
        }

        return 0;
    }

    private static bool TryReceiveStreamFrame(Zlink.Socket socket,
        Span<byte> buffer, out int copied)
    {
        copied = 0;
        try
        {
            using Message msg = socket.ReceiveMessage(ReceiveFlags.None);
            ReadOnlySpan<byte> frame = msg.AsReadOnlySpan();
            copied = Math.Min(frame.Length, buffer.Length);
            if (copied > 0)
                frame.Slice(0, copied).CopyTo(buffer);
            return true;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
            return false;
        }
    }
}

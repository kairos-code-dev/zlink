using System;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    private static int ParsePerfOnly(string name, int defaultValue)
    {
        string? raw = Environment.GetEnvironmentVariable(name);
        if (string.IsNullOrWhiteSpace(raw))
            return defaultValue;
        return int.TryParse(raw, out int parsed) && parsed > 0
            ? parsed
            : defaultValue;
    }

    internal static int RunStream(string transport, int size)
    {
        return RunStreamInternal(transport, size, len32beDispatch: false,
            patternName: "STREAM", endpointName: "stream");
    }

    internal static int RunStreamCallback(string transport, int size)
    {
        return RunStreamInternal(transport, size, len32beDispatch: false,
            patternName: "STREAM_CALLBACK", endpointName: "stream-callback");
    }

    internal static int RunStreamLen32Be(string transport, int size)
    {
        return RunStreamInternal(transport, size, len32beDispatch: true,
            patternName: "STREAM_LEN32BE", endpointName: "stream-len32be");
    }

    private static int RunStreamInternal(string transport, int size,
        bool len32beDispatch, string patternName, string endpointName)
    {
        int warmup = ParseEnv("PERF_WARMUP_COUNT", 1000);
        int latCount = ParseEnv("PERF_LAT_COUNT", 500);
        int msgCount = ParseEnv("PERF_MSG_COUNT", 10000);
        int inflightLimit = 10;
        int ioTimeoutMs = ParsePerfOnly("PERF_STREAM_TIMEOUT_MS", 5000);
        int drainTimeoutMs = ParsePerfOnly("PERF_STREAM_DRAIN_TIMEOUT_MS",
            Math.Max(ioTimeoutMs * 6, 30000));

        using var ctx = new Context();
        using var server = new Zlink.Socket(ctx, SocketType.Stream);
        using var client = new Zlink.Socket(ctx, SocketType.Stream);
        using var echo = new PerfStreamCallbackEcho(server, len32beDispatch, echo: true);
        using var sink = new PerfStreamCallbackEcho(client, len32beDispatch, echo: false);

        try
        {
            string ep = EndpointFor(transport, endpointName);
            server.SetOption(SocketOption.SndTimeo, ioTimeoutMs);
            server.SetOption(SocketOption.RcvTimeo, ioTimeoutMs);
            client.SetOption(SocketOption.SndTimeo, ioTimeoutMs);
            client.SetOption(SocketOption.RcvTimeo, ioTimeoutMs);

            echo.Attach();
            sink.Attach();

            server.Bind(ep);
            client.Connect(ep);
            Thread.Sleep(300);

            byte[] clientServerRid = PerfStreamCallbackEcho.WaitPeerRoutingId(
                client, ioTimeoutMs);
            byte[] payload = new byte[size];
            Array.Fill(payload, (byte)'a');
            byte[] sendBuf = len32beDispatch ? payload : EncodeLen32Be(payload);

            bool WaitReceived(long target, int timeoutMs)
            {
                var deadline = DateTime.UtcNow.AddMilliseconds(Math.Max(timeoutMs, 1));
                while (DateTime.UtcNow < deadline)
                {
                    if (sink.Received >= target)
                        return true;
                    Thread.Sleep(1);
                }
                return sink.Received >= target;
            }

            long expected = sink.Received;
            for (int i = 0; i < warmup; i++)
            {
                PerfStreamCallbackEcho.Send(client, clientServerRid, sendBuf);
                expected++;
                if (!WaitReceived(expected, ioTimeoutMs))
                    throw new TimeoutException("STREAM warmup timeout");
            }

            var sw = System.Diagnostics.Stopwatch.StartNew();
            for (int i = 0; i < latCount; i++)
            {
                PerfStreamCallbackEcho.Send(client, clientServerRid, sendBuf);
                expected++;
                if (!WaitReceived(expected, ioTimeoutMs))
                    throw new TimeoutException("STREAM latency timeout");
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

            long recvBegin = sink.Received;
            int sent = 0;
            sw.Restart();
            for (int i = 0; i < msgCount; i++)
            {
                try
                {
                    PerfStreamCallbackEcho.Send(client, clientServerRid, sendBuf);
                }
                catch
                {
                    break;
                }
                sent++;
                long acked = Math.Max(0L, sink.Received - recvBegin);
                if ((long)sent - acked >= inflightLimit)
                {
                    long target = recvBegin + sent - inflightLimit + 1L;
                    if (!WaitReceived(target, ioTimeoutMs))
                        break;
                }
            }

            if (sent > 0)
                WaitReceived(recvBegin + sent, drainTimeoutMs);
            sw.Stop();

            long recvDelta = Math.Max(0, sink.Received - recvBegin);
            int effective = Math.Min(sent, checked((int)recvDelta));
            double thr = (effective > 0 && sw.Elapsed.TotalSeconds > 0.0)
                ? (effective / sw.Elapsed.TotalSeconds)
                : 0.0;
            if (effective <= 0)
                return 2;

            PrintResult(patternName, transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }

    private static byte[] EncodeLen32Be(byte[] payload)
    {
        var frame = new byte[payload.Length + 4];
        int size = payload.Length;
        frame[0] = (byte)((size >> 24) & 0xFF);
        frame[1] = (byte)((size >> 16) & 0xFF);
        frame[2] = (byte)((size >> 8) & 0xFF);
        frame[3] = (byte)(size & 0xFF);
        Buffer.BlockCopy(payload, 0, frame, 4, payload.Length);
        return frame;
    }
}

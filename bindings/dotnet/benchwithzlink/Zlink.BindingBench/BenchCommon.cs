using System;
using System.Collections.Generic;
using System.Net;
using System.Threading;
using Zlink;
using TcpListener = System.Net.Sockets.TcpListener;

internal static partial class BenchRunner
{
    private const int ErrnoEintr = 4;
    private const int ErrnoEagain = 11;

    internal static int ReceiveRetry(Zlink.Socket socket, Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        while (true)
        {
            if (socket.TryReceive(buffer, out int bytesReceived, out int errno,
                flags))
            {
                return bytesReceived;
            }
            if (errno == ErrnoEintr)
                continue;
            throw ZlinkException.FromLastError();
        }
    }

    internal static int ReceiveRetry(Zlink.Socket socket, byte[] buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return ReceiveRetry(socket, buffer.AsSpan(), flags);
    }

    internal static int SendRetry(Zlink.Socket socket, ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        while (true)
        {
            if (socket.TrySend(buffer, out int bytesSent, out int errno, flags))
                return bytesSent;
            if (errno == ErrnoEintr)
                continue;
            throw ZlinkException.FromLastError();
        }
    }

    internal static int SendRetry(Zlink.Socket socket, byte[] buffer,
        SendFlags flags = SendFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return SendRetry(socket, buffer.AsSpan(), flags);
    }

    internal static bool WaitForInput(Zlink.Socket socket, int timeoutMs)
    {
        var poller = new Poller();
        poller.Add(socket, PollEvents.PollIn);
        var events = new List<PollEvent>();
        return poller.Wait(events, timeoutMs) > 0;
    }

    internal static bool WaitUntil(Func<bool> check, int timeoutMs, int intervalMs = 10)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                if (check())
                    return true;
            }
            catch
            {
            }
            Thread.Sleep(intervalMs);
        }
        return false;
    }

    internal static void GatewayReceiveProviderMessage(Zlink.Socket router,
        Span<byte> routingIdBuffer, Span<byte> payloadBuffer)
    {
        int idLen = ReceiveRetry(router, routingIdBuffer, ReceiveFlags.None);
        if (idLen <= 0 || router.GetOption(SocketOption.RcvMore) == 0)
            throw new InvalidOperationException(
                "Gateway provider message missing routing frame.");

        int payloadLen = ReceiveRetry(router, payloadBuffer, ReceiveFlags.None);
        if (payloadLen < 0)
            throw new InvalidOperationException(
                "Gateway provider message payload receive failed.");

        // Gateway benchmark sends 1 payload frame, but drain extras to keep
        // the stream aligned if additional parts ever appear.
        if (router.GetOption(SocketOption.RcvMore) != 0)
        {
            Span<byte> discard = stackalloc byte[256];
            while (router.GetOption(SocketOption.RcvMore) != 0)
                ReceiveRetry(router, discard, ReceiveFlags.None);
        }
    }

    internal static int SpotReceivePayloadWithTimeout(Spot spot,
        Span<byte> payloadBuffer, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            if (spot.TryReceiveSinglePayload(payloadBuffer, out int payloadLen,
                out int errno, ReceiveFlags.DontWait))
            {
                return payloadLen;
            }
            if (errno == ErrnoEagain || errno == ErrnoEintr)
                Thread.Sleep(1);
            else
                throw ZlinkException.FromLastError();
        }
        throw new TimeoutException();
    }

    internal static int StreamExpectConnectEvent(Zlink.Socket socket,
        Span<byte> idBuffer)
    {
        // STREAM can emit non-connect notifications first; keep consuming
        // event pairs until a connect notification (0x01) arrives.
        Span<byte> payload = stackalloc byte[16];
        for (int attempt = 0; attempt < 64; attempt++)
        {
            int idLen = ReceiveRetry(socket, idBuffer, ReceiveFlags.None);
            int pLen = ReceiveRetry(socket, payload, ReceiveFlags.None);
            if (pLen == 1 && payload[0] == 0x01)
            {
                int safeLen = idLen;
                if (safeLen < 0)
                    safeLen = 0;
                if (safeLen > idBuffer.Length)
                    safeLen = idBuffer.Length;
                return safeLen;
            }
        }
        throw new InvalidOperationException("STREAM connect event not observed");
    }

    internal static void StreamSend(Zlink.Socket socket, ReadOnlySpan<byte> id,
        ReadOnlySpan<byte> payload)
    {
        SendRetry(socket, id, SendFlags.SendMore);
        SendRetry(socket, payload, SendFlags.None);
    }

    internal static void StreamSend(Zlink.Socket socket, byte[] id, byte[] payload)
    {
        if (id == null)
            throw new ArgumentNullException(nameof(id));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        StreamSend(socket, id.AsSpan(), payload.AsSpan());
    }

    internal static void StreamRecv(Zlink.Socket socket, Span<byte> idBuffer,
        Span<byte> payloadBuffer, out int idLength, out int payloadLength)
    {
        int idLen = ReceiveRetry(socket, idBuffer, ReceiveFlags.None);
        int n = ReceiveRetry(socket, payloadBuffer, ReceiveFlags.None);

        idLength = idLen;
        if (idLength < 0)
            idLength = 0;
        if (idLength > idBuffer.Length)
            idLength = idBuffer.Length;

        payloadLength = n;
        if (payloadLength < 0)
            payloadLength = 0;
        if (payloadLength > payloadBuffer.Length)
            payloadLength = payloadBuffer.Length;
    }

    internal static int StreamRecvPayload(Zlink.Socket socket,
        Span<byte> idBuffer, Span<byte> payloadBuffer)
    {
        ReceiveRetry(socket, idBuffer, ReceiveFlags.None);

        int payloadLen = ReceiveRetry(socket, payloadBuffer, ReceiveFlags.None);
        if (payloadLen < 0)
            return 0;
        if (payloadLen > payloadBuffer.Length)
            return payloadBuffer.Length;
        return payloadLen;
    }

    internal static void PrintResult(string pattern, string transport, int size, double thr, double latUs)
    {
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},throughput,{thr}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},latency,{latUs}");
    }

    internal static int ParseEnv(string name, int defaultValue)
    {
        var v = Environment.GetEnvironmentVariable(name);
        return int.TryParse(v, out var p) && p > 0 ? p : defaultValue;
    }

    internal static int ResolveMsgCount(int size)
    {
        var v = Environment.GetEnvironmentVariable("BENCH_MSG_COUNT");
        if (int.TryParse(v, out var p) && p > 0)
            return p;
        return size <= 1024 ? 200000 : 20000;
    }

    internal static string EndpointFor(string transport, string name)
    {
        if (transport == "inproc")
            return $"inproc://bench-{name}-{Guid.NewGuid()}";
        return $"{transport}://127.0.0.1:{GetPort()}";
    }

    private static int GetPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }
}

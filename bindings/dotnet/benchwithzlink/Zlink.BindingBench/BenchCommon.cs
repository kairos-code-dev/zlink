using System;
using System.Collections.Generic;
using System.Net;
using System.Threading;
using Zlink;
using TcpListener = System.Net.Sockets.TcpListener;

internal static partial class BenchRunner
{
    private const int ErrnoEintr = 4;

    internal static int ReceiveRetry(Zlink.Socket socket, byte[] buffer, ReceiveFlags flags = ReceiveFlags.None)
    {
        while (true)
        {
            try
            {
                return socket.Receive(buffer, flags);
            }
            catch (ZlinkException ex) when (ex.Errno == ErrnoEintr)
            {
                continue;
            }
        }
    }

    internal static int SendRetry(Zlink.Socket socket, byte[] buffer, SendFlags flags = SendFlags.None)
    {
        while (true)
        {
            try
            {
                return socket.Send(buffer, flags);
            }
            catch (ZlinkException ex) when (ex.Errno == ErrnoEintr)
            {
                continue;
            }
        }
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

    internal static int ReceiveWithTimeout(Zlink.Socket socket, byte[] buffer, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return socket.Receive(buffer, ReceiveFlags.DontWait);
            }
            catch
            {
                Thread.Sleep(1);
            }
        }
        throw new TimeoutException();
    }

    internal static void GatewaySendWithRetry(Gateway gateway, string service, byte[] payload, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                gateway.Send(service, new[] { Message.FromBytes(payload) }, SendFlags.None);
                return;
            }
            catch
            {
                Thread.Sleep(1);
            }
        }
        throw new TimeoutException();
    }

    internal static SpotMessage SpotReceiveWithTimeout(Spot spot, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return spot.Receive(ReceiveFlags.DontWait);
            }
            catch
            {
                Thread.Sleep(1);
            }
        }
        throw new TimeoutException();
    }

    internal static byte[] StreamExpectConnectEvent(Zlink.Socket socket)
    {
        // STREAM can emit non-connect notifications first; keep consuming
        // event pairs until a connect notification (0x01) arrives.
        for (int attempt = 0; attempt < 64; attempt++)
        {
            var id = new byte[256];
            int idLen = ReceiveRetry(socket, id, ReceiveFlags.None);
            var payload = new byte[16];
            int pLen = ReceiveRetry(socket, payload, ReceiveFlags.None);
            if (pLen == 1 && payload[0] == 0x01)
            {
                int safeLen = idLen;
                if (safeLen < 0)
                    safeLen = 0;
                if (safeLen > id.Length)
                    safeLen = id.Length;
                return id.AsSpan(0, safeLen).ToArray();
            }
        }
        throw new InvalidOperationException("STREAM connect event not observed");
    }

    internal static void StreamSend(Zlink.Socket socket, byte[] id, byte[] payload)
    {
        SendRetry(socket, id, SendFlags.SendMore);
        SendRetry(socket, payload, SendFlags.None);
    }

    internal static (byte[] Id, byte[] Payload) StreamRecv(Zlink.Socket socket, int cap)
    {
        var id = new byte[256];
        int idLen = ReceiveRetry(socket, id, ReceiveFlags.None);
        var payload = new byte[cap];
        int n = ReceiveRetry(socket, payload, ReceiveFlags.None);
        int safeIdLen = idLen;
        if (safeIdLen < 0)
            safeIdLen = 0;
        if (safeIdLen > id.Length)
            safeIdLen = id.Length;
        int safeN = n;
        if (safeN < 0)
            safeN = 0;
        if (safeN > payload.Length)
            safeN = payload.Length;
        return (id.AsSpan(0, safeIdLen).ToArray(), payload.AsSpan(0, safeN).ToArray());
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

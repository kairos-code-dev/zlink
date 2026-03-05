using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Threading;
using Zlink;
using TcpListener = System.Net.Sockets.TcpListener;

internal static partial class PerfRunner
{
    private const int ErrnoEintr = 4;
    private const int ErrnoEagain = 11;
    private static readonly object IpcLock = new();
    private static readonly HashSet<string> IpcPaths = new();
    private static bool IpcCleanupHooked;

    internal static int ReceiveRetry(Zlink.Socket socket, Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        ReceiveFlags operationFlags = flags | ReceiveFlags.DontWait;
        while (true)
        {
            try
            {
                return socket.Receive(buffer, operationFlags);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
            {
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
            {
                Thread.Sleep(1);
                continue;
            }
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
        SendFlags operationFlags = flags | SendFlags.DontWait;
        while (true)
        {
            try
            {
                return socket.Send(buffer, operationFlags);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
            {
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
            {
                Thread.Sleep(1);
                continue;
            }
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
        if (idLen <= 0 || router.GetOption(SocketOptions.RcvMore) == 0)
            throw new InvalidOperationException(
                "Gateway provider message missing routing frame.");

        int payloadLen = ReceiveRetry(router, payloadBuffer, ReceiveFlags.None);
        if (payloadLen < 0)
            throw new InvalidOperationException(
                "Gateway provider message payload receive failed.");

        // Gateway benchmark sends 1 payload frame, but drain extras to keep
        // the stream aligned if additional parts ever appear.
        if (router.GetOption(SocketOptions.RcvMore) != 0)
        {
            Span<byte> discard = stackalloc byte[256];
            while (router.GetOption(SocketOptions.RcvMore) != 0)
                ReceiveRetry(router, discard, ReceiveFlags.None);
        }
    }

    internal static int SpotReceivePayloadWithTimeout(Spot spot,
        Span<byte> payloadBuffer, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return spot.ReceiveSinglePayload(payloadBuffer,
                    ReceiveFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                || IsInterrupted(ex.Errno))
            {
                Thread.Sleep(1);
            }
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
            int idLen = ReceiveMessageToSpan(socket, idBuffer, ReceiveFlags.None);
            int pLen = ReceiveMessageToSpan(socket, payload, ReceiveFlags.None);
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
        int idLen = ReceiveMessageToSpan(socket, idBuffer, ReceiveFlags.None);
        int n = ReceiveMessageToSpan(socket, payloadBuffer, ReceiveFlags.None);

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
        ReceiveMessageToSpan(socket, idBuffer, ReceiveFlags.None);
        int payloadLen = ReceiveMessageToSpan(socket, payloadBuffer, ReceiveFlags.None);
        if (payloadLen < 0)
            return 0;
        if (payloadLen > payloadBuffer.Length)
            return payloadBuffer.Length;
        return payloadLen;
    }

    private static int ReceiveMessageToSpan(Zlink.Socket socket, Span<byte> buffer,
        ReceiveFlags flags)
    {
        using var msg = socket.ReceiveMessage(flags);
        int size = msg.Size;
        if (size <= 0)
            return 0;
        int copyLen = Math.Min(size, buffer.Length);
        if (copyLen > 0)
            msg.AsReadOnlySpan().Slice(0, copyLen).CopyTo(buffer);
        return size;
    }

    internal static void PrintResult(string pattern, string transport, int size, double thr, double latUs)
    {
        double bw = BandwidthMbps(pattern, thr, size);
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},throughput,{thr}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},bandwidth,{bw}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},latency,{latUs}");
    }

    private static double BandwidthMbps(string pattern, double throughput, int size)
    {
        bool isEcho = pattern.Equals("MULTI_DEALER_ROUTER",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_ROUTER_ROUTER",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM", StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_CALLBACK",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_LEN32BE",
                StringComparison.OrdinalIgnoreCase);
        double multiplier = isEcho ? 2.0 : 1.0;
        return (throughput * size * multiplier) / 1_000_000.0;
    }

    internal static int ParseEnv(string name, int defaultValue)
    {
        var v = Environment.GetEnvironmentVariable(name);
        return int.TryParse(v, out var p) && p > 0 ? p : defaultValue;
    }

    private static int ParseEnvNonNegative(string name, int defaultValue)
    {
        var v = Environment.GetEnvironmentVariable(name);
        return int.TryParse(v, out var p) && p >= 0 ? p : defaultValue;
    }

    private static int ResolveSingleHwmValue(string specificName)
    {
        int hwm = ParseEnvNonNegative("PERF_SINGLE_HWM", 0);
        int specific = ParseEnvNonNegative(specificName, 0);
        return specific > 0 ? specific : hwm;
    }

    internal static void ApplySingleContextOptions(Context ctx)
    {
        int ioThreads = ParseEnvNonNegative("PERF_IO_THREADS", 0);
        if (ioThreads > 0)
            ctx.SetOption(ContextOption.IoThreads, ioThreads);
    }

    internal static void ApplySingleSocketOptions(Zlink.Socket socket)
    {
        int sndHwm = ResolveSingleHwmValue("PERF_SINGLE_SNDHWM");
        int rcvHwm = ResolveSingleHwmValue("PERF_SINGLE_RCVHWM");
        int sndTimeo = ParseEnvNonNegative("PERF_SINGLE_SNDTIMEO_MS", 0);
        int rcvTimeo = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 0);

        if (sndHwm > 0)
            socket.SetOption(SocketOptions.SndHwm, sndHwm);
        if (rcvHwm > 0)
            socket.SetOption(SocketOptions.RcvHwm, rcvHwm);
        if (sndTimeo > 0)
            socket.SetOption(SocketOptions.SndTimeo, sndTimeo);
        if (rcvTimeo > 0)
            socket.SetOption(SocketOptions.RcvTimeo, rcvTimeo);
    }

    internal static int ResolveMsgCount(int size)
    {
        var v = Environment.GetEnvironmentVariable("PERF_MSG_COUNT");
        if (int.TryParse(v, out var p) && p > 0)
            return p;
        return size <= 1024 ? 200000 : 20000;
    }

    private static bool IsWouldBlock(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EAgain || errno == ErrnoEagain;
    }

    private static bool IsInterrupted(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EIntr || errno == ErrnoEintr;
    }

    internal static string EndpointFor(string transport, string name)
    {
        if (transport == "inproc")
            return $"inproc://bench-{name}-{Guid.NewGuid()}";
        if (transport == "ipc")
        {
            string endpoint = $"ipc:///tmp/zlink-bench-{name}-{GetPort()}.sock";
            RegisterIpcEndpoint(endpoint);
            return endpoint;
        }
        return $"{transport}://127.0.0.1:{GetPort()}";
    }

    private static void RegisterIpcEndpoint(string endpoint)
    {
        const string prefix = "ipc://";
        if (!endpoint.StartsWith(prefix, StringComparison.Ordinal))
            return;
        string path = endpoint.Substring(prefix.Length);
        if (path.Length == 0 || path[0] != '/')
            return;

        lock (IpcLock)
        {
            IpcPaths.Add(path);
            TryDeleteFile(path);
            if (!IpcCleanupHooked)
            {
                AppDomain.CurrentDomain.ProcessExit += (_, _) => CleanupIpcFiles();
                IpcCleanupHooked = true;
            }
        }
    }

    private static void CleanupIpcFiles()
    {
        List<string> snapshot;
        lock (IpcLock)
            snapshot = new List<string>(IpcPaths);

        foreach (var path in snapshot)
            TryDeleteFile(path);
    }

    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path))
                File.Delete(path);
        }
        catch
        {
            // Ignore best-effort cleanup errors during benchmark runs.
        }
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

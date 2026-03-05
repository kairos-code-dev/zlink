using System;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    private static readonly byte[] MultiStopToken =
        System.Text.Encoding.ASCII.GetBytes("__zlink_perf_stop__");

    private const int MaxStreamFrameBytes = 16 * 1024 * 1024;

    private static bool IsMultiStreamPattern(string pattern)
    {
        return pattern.Equals("MULTI_STREAM", StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_CALLBACK",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_LEN32BE",
                StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsMultiStreamCallbackPattern(string pattern)
    {
        return pattern.Equals("MULTI_STREAM_CALLBACK",
                StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_STREAM_LEN32BE",
                StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsMultiLen32BePattern(string pattern)
    {
        return pattern.Equals("MULTI_STREAM_LEN32BE",
            StringComparison.OrdinalIgnoreCase);
    }

    private static int ResolveMultiDrainMs(string pattern)
    {
        int defaultDrain = pattern.Equals("MULTI_GATEWAY",
                               StringComparison.OrdinalIgnoreCase)
                               || pattern.Equals("MULTI_SPOT",
                                   StringComparison.OrdinalIgnoreCase)
            ? 0
            : 300;
        return ParseNonNegativeEnv("PERF_MULTI_DRAIN_MS", defaultDrain);
    }

    private static string MultiEndpointFor(string transport, string name)
    {
        int bindPort = ParseNonNegativeEnv("PERF_MULTI_SERVER_BIND_PORT", 0);
        if (bindPort > 0)
            return $"{transport}://127.0.0.1:{bindPort}";
        return EndpointFor(transport, name);
    }

    private static bool IsMonitorReady(SocketEvent eventValue, bool acceptFallback)
    {
        if (eventValue == SocketEvent.ConnectionReady)
            return true;
        if (!acceptFallback)
            return false;
        return eventValue == SocketEvent.Accepted
            || eventValue == SocketEvent.Connected;
    }

    private static bool WaitMonitorReady(MonitorSocket monitor, int timeoutMs,
        bool acceptFallback)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(Math.Max(1, timeoutMs));
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                MonitorEvent evt = monitor.Receive(ReceiveFlags.DontWait);
                if (IsMonitorReady(evt.Event, acceptFallback))
                    return true;
            }
            catch (ZlinkException ex) when (ex.Errno == ErrnoEagain
                                            || ex.Errno == ErrnoEintr)
            {
                Thread.Sleep(1);
            }
            catch
            {
                return false;
            }
        }
        return false;
    }

    private static Zlink.SocketType ResolveServerSocketType(string pattern)
    {
        if (pattern.Equals("MULTI_DEALER_ROUTER", StringComparison.OrdinalIgnoreCase)
            || pattern.Equals("MULTI_ROUTER_ROUTER",
                StringComparison.OrdinalIgnoreCase))
        {
            return Zlink.SocketType.Router;
        }
        if (pattern.Equals("MULTI_PUBSUB", StringComparison.OrdinalIgnoreCase))
            return Zlink.SocketType.Sub;
        return Zlink.SocketType.Dealer;
    }

    private static Zlink.SocketType ResolveClientSocketType(string pattern)
    {
        if (pattern.Equals("MULTI_PUBSUB", StringComparison.OrdinalIgnoreCase))
            return Zlink.SocketType.Pub;
        return Zlink.SocketType.Dealer;
    }

    private static bool IsStopTokenPayload(ReadOnlySpan<byte> payload)
    {
        return payload.Length == MultiStopToken.Length
            && payload.SequenceEqual(MultiStopToken);
    }

    private static int ResolveMultiHwmValue(string specificName)
    {
        int hwm = ParseNonNegativeEnv("PERF_MULTI_HWM", 0);
        int specific = ParseNonNegativeEnv(specificName, 0);
        return specific > 0 ? specific : hwm;
    }

    private static int ResolveIoThreads(string primaryName)
    {
        int primary = ParseNonNegativeEnv(primaryName, 0);
        if (primary > 0)
            return primary;
        return ParseNonNegativeEnv("PERF_IO_THREADS", 0);
    }

    internal static void ApplyMultiServerContextOptions(Context ctx)
    {
        int ioThreads = ResolveIoThreads("PERF_MULTI_SERVER_IO_THREADS");
        if (ioThreads > 0)
            ctx.SetOption(ContextOption.IoThreads, ioThreads);
    }

    internal static void ApplyMultiClientContextOptions(Context ctx)
    {
        int ioThreads = ResolveIoThreads("PERF_MULTI_CLIENT_IO_THREADS");
        if (ioThreads > 0)
            ctx.SetOption(ContextOption.IoThreads, ioThreads);
    }

    internal static void ApplyMultiSocketOptions(Zlink.Socket socket)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_MULTI_SNDHWM");
        int rcvHwm = ResolveMultiHwmValue("PERF_MULTI_RCVHWM");
        int sndTimeo = ParseNonNegativeEnv("PERF_MULTI_SNDTIMEO_MS", 0);
        int rcvTimeo = ParseNonNegativeEnv("PERF_MULTI_RCVTIMEO_MS", 0);

        if (sndHwm > 0)
            socket.SetOption(SocketOptions.SndHwm, sndHwm);
        if (rcvHwm > 0)
            socket.SetOption(SocketOptions.RcvHwm, rcvHwm);
        if (sndTimeo > 0)
            socket.SetOption(SocketOptions.SndTimeo, sndTimeo);
        if (rcvTimeo > 0)
            socket.SetOption(SocketOptions.RcvTimeo, rcvTimeo);
    }
}

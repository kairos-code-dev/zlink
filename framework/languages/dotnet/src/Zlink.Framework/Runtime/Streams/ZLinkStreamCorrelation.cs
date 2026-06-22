namespace Zlink.Framework.Runtime.Streams;

// Process-global monotonic correlation id for framework-originated outbound stream
// packets (hex of a per-process counter). Mirrors the connector's generator and the
// C++ client_call_codec: unique within a process, propagated to correlate nodes
// (client request -> server echo). Server replies echo the request id instead.
internal static class ZLinkStreamCorrelation
{
    private static long _counter;

    public static string Next() =>
        System.Convert.ToString(System.Threading.Interlocked.Increment(ref _counter), 16);
}

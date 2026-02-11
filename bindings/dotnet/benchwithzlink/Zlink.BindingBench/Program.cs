using System.Diagnostics;
using System.Net;
using Zlink;
using TcpListener = System.Net.Sockets.TcpListener;

if (args.Length < 3)
    return 1;

var pattern = args[0].ToUpperInvariant();
var transport = args[1];
if (!int.TryParse(args[2], out var size))
    return 1;
var coreDir = args.Length >= 4 ? args[3] : string.Empty;

if (pattern != "PAIR")
    return RunCore(pattern, transport, args[2], coreDir);

int warmup = ParseEnv("BENCH_WARMUP_COUNT", 1000);
int latCount = ParseEnv("BENCH_LAT_COUNT", 500);
int msgCount = ResolveMsgCount(size);

using var ctx = new Context();
using var a = new Zlink.Socket(ctx, Zlink.SocketType.Pair);
using var b = new Zlink.Socket(ctx, Zlink.SocketType.Pair);

try
{
    var ep = EndpointFor(transport);
    a.Bind(ep);
    b.Connect(ep);
    Thread.Sleep(50);
    var buf = new byte[size];
    Array.Fill(buf, (byte)'a');
    var recv = new byte[size];

    for (int i = 0; i < warmup; i++)
    {
        b.Send(buf, SendFlags.None);
        a.Receive(recv, ReceiveFlags.None);
    }

    var sw = Stopwatch.StartNew();
    for (int i = 0; i < latCount; i++)
    {
        b.Send(buf, SendFlags.None);
        a.Receive(recv, ReceiveFlags.None);
        a.Send(recv, SendFlags.None);
        b.Receive(recv, ReceiveFlags.None);
    }
    sw.Stop();
    double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

    sw.Restart();
    for (int i = 0; i < msgCount; i++)
        b.Send(buf, SendFlags.None);
    for (int i = 0; i < msgCount; i++)
        a.Receive(recv, ReceiveFlags.None);
    sw.Stop();

    double thr = msgCount / sw.Elapsed.TotalSeconds;
    Console.WriteLine($"RESULT,current,PAIR,{transport},{size},throughput,{thr}");
    Console.WriteLine($"RESULT,current,PAIR,{transport},{size},latency,{latUs}");
    return 0;
}
catch
{
    return 2;
}

static int ParseEnv(string name, int defaultValue)
{
    var v = Environment.GetEnvironmentVariable(name);
    return int.TryParse(v, out var p) && p > 0 ? p : defaultValue;
}

static int ResolveMsgCount(int size)
{
    var v = Environment.GetEnvironmentVariable("BENCH_MSG_COUNT");
    if (int.TryParse(v, out var p) && p > 0)
        return p;
    return size <= 1024 ? 200000 : 20000;
}

static string EndpointFor(string transport)
{
    if (transport == "inproc")
        return $"inproc://bench-pair-{Guid.NewGuid()}";
    return $"{transport}://127.0.0.1:{GetPort()}";
}

static int GetPort()
{
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    int port = ((IPEndPoint)listener.LocalEndpoint).Port;
    listener.Stop();
    return port;
}

static int RunCore(string pattern, string transport, string sizeArg, string coreDirArg)
{
    string? bin = pattern switch
    {
        "PUBSUB" => "comp_current_pubsub",
        "DEALER_DEALER" => "comp_current_dealer_dealer",
        "DEALER_ROUTER" => "comp_current_dealer_router",
        "ROUTER_ROUTER" => "comp_current_router_router",
        "ROUTER_ROUTER_POLL" => "comp_current_router_router_poll",
        "STREAM" => "comp_current_stream",
        "GATEWAY" => "comp_current_gateway",
        "SPOT" => "comp_current_spot",
        _ => null,
    };

    if (bin == null)
        return 0;

    var coreDir = !string.IsNullOrEmpty(coreDirArg)
        ? coreDirArg
        : (Environment.GetEnvironmentVariable("ZLINK_CORE_BENCH_DIR") ?? string.Empty);
    if (string.IsNullOrEmpty(coreDir))
    {
        Console.Error.WriteLine($"core bench dir is required for pattern {pattern}");
        return 2;
    }

    var psi = new ProcessStartInfo
    {
        FileName = Path.Combine(coreDir, bin),
        Arguments = $"current {transport} {sizeArg}",
        UseShellExecute = false,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
    };

    using var p = Process.Start(psi);
    if (p == null)
        return 2;
    Console.Write(p.StandardOutput.ReadToEnd());
    Console.Error.Write(p.StandardError.ReadToEnd());
    p.WaitForExit();
    return p.ExitCode;
}

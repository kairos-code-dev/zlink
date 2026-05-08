using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var server = new PairSocket(ctx);
using var client = new PairSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var serverMonitor = server.MonitorOpen(SocketEvent.ConnectionReady);
using var clientMonitor = client.MonitorOpen(SocketEvent.ConnectionReady);
if (serverMonitor.Recv(RecvFlags.DontWait) != null)
    throw new InvalidOperationException("monitor sample expected empty server Recv(true)");
if (clientMonitor.Recv(RecvFlags.DontWait) != null)
    throw new InvalidOperationException("monitor sample expected empty client Recv(true)");
server.Bind(endpoint);
client.Connect(endpoint);

MonitorEvent serverEvent = serverMonitor.Recv();
MonitorEvent clientEvent = clientMonitor.Recv();
if (serverEvent.Event != MonitorEventType.ConnectionReady
    || clientEvent.Event != MonitorEventType.ConnectionReady)
{
    throw new InvalidOperationException("monitor sample expected ConnectionReady events");
}
if (serverMonitor.Recv(RecvFlags.DontWait) != null)
    throw new InvalidOperationException("monitor sample expected empty server Recv(true)");
if (clientMonitor.Recv(RecvFlags.DontWait) != null)
    throw new InvalidOperationException("monitor sample expected empty client Recv(true)");

Console.WriteLine("[monitor/recv] recv: \"connection-ready\" -> Recv(true): empty");

using System.Threading;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var sender = new PairSocket(ctx);
using var receiver = new PairSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var senderMonitor = sender.MonitorOpen(SocketEvent.ConnectionReady);
using var receiverMonitor = receiver.MonitorOpen(SocketEvent.ConnectionReady);
sender.Bind(endpoint);
receiver.Connect(endpoint);
SampleSupport.WaitConnected(senderMonitor, receiverMonitor);

using var signal = new ManualResetEventSlim(false);
string? payload = null;
receiver.OnReceive((routingId, parts) =>
{
    using (parts[0])
        payload = parts[0].GetString();
    signal.Set();
});

using (Message message = Message.FromString("hello-pair"))
    sender.Send(message);
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000, "pair callback timeout");
Console.WriteLine(
    $"[pair/callback] send: \"hello-pair\" -> recv: \"{payload}\"");

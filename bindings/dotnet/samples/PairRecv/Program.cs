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

const string payload = "hello-pair";
using (Message message = Message.FromString(payload))
    sender.Send(message);
string receivedPayload = SampleSupport.ReceiveUtf8(receiver, 2000);
Console.WriteLine(
    $"[pair/recv] send: \"hello-pair\" -> recv: \"{receivedPayload}\"");

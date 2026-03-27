using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var sender = new PairSocket(ctx);
using var receiver = new PairSocket(ctx);
string endpoint = SampleSupport.NewEndpoint("inproc", "pair-recv");
sender.Bind(endpoint);
receiver.Connect(endpoint);

SampleSupport.SendUtf8UntilReady(sender, "pair-recv", 2000);
string payload = SampleSupport.ReceiveUtf8(receiver, 2000);
Console.WriteLine(payload);

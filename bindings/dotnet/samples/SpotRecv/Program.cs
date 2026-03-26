using SampleCommon;
using Zlink;
using Zlink.Service;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var spot = new Spot(ctx);
spot.SetSubscription("spot:sample");
using var message = Message.FromString("spot-recv");
spot.Publish("spot:sample", message);
using ServiceMonitor monitor = spot.OpenMonitor();
monitor.Close();
Console.WriteLine("spot:sample");

using RuntimeMonitoring.Server.Trigger;
using RuntimeMonitoring.Server.Trigger.Support;

var app = TriggerHostFactory.Create(args);
await app.RunAsync();

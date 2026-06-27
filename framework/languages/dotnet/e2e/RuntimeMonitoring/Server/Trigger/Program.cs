using RuntimeMonitoring.Trigger;

var app = TriggerHostFactory.Create(args);
await app.RunAsync();

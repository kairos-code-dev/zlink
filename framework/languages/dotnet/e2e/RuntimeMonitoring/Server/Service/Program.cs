using RuntimeMonitoring.Server.Service;
using RuntimeMonitoring.Server.Service.Handlers;
using RuntimeMonitoring.Server.Service.Support;

var app = ServiceHostFactory.CreateAll(args);
await app.RunAsync();

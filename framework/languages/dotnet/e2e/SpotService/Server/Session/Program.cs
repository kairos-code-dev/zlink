using SpotService.Server.Session;
using SpotService.Server.Session.Handlers;
using SpotService.Server.Session.Spots;

var app = SessionHostFactory.Create(args);
await app.RunAsync();

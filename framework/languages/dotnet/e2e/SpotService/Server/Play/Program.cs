using SpotService.Server.Play;
using SpotService.Server.Play.Endpoints;
using SpotService.Server.Play.Handlers;
using SpotService.Server.Play.Spots;

var app = PlayHostFactory.Create(args);
await app.RunAsync();

using SpotService.Server.MultiNode;
using SpotService.Server.MultiNode.Handlers;
using SpotService.Server.MultiNode.Spots;

var app = MultiNodeHostFactory.Create(args);
await app.RunAsync();

using Microsoft.Extensions.Hosting;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play;

var settings = SampleSettings.Load(args, "play");
using var server = new PlayServer(settings).Build();
await server.RunAsync();

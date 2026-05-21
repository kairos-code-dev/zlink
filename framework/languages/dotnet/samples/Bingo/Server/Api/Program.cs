using Bingo.Server.Api;
using Bingo.Shared.Configuration;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();
await ApiServerHostFactory.Build(topology).RunAsync();

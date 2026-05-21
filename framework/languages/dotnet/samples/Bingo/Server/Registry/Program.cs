using Bingo.Server.Registry;
using Bingo.Shared.Configuration;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();
await RegistryHostFactory.Build(topology).RunAsync();

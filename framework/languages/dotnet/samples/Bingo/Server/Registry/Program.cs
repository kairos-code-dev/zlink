using Bingo.Server.Infrastructure.Configuration;
using Bingo.Server.Registry;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();
await RegistryHostFactory.Build(topology).RunAsync();

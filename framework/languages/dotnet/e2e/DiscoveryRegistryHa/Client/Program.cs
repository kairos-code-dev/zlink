using DiscoveryRegistryHa.Client.Scenarios;
using DiscoveryRegistryHa.Client.Support;

var options = ClientOptions.Parse(args);

if (options.Scenario is "a1")
{
    await BasicDiscoveryScenario.RunAsync(options);
}
else if (options.Scenario is "dr-a2")
{
    await DrA2ClusterBridgeScenario.RunAsync(options);
}
else if (options.Scenario is "dr-a3")
{
    await DrA3ClusterBridgeScenario.RunAsync(options);
}
else if (options.Scenario is "dr-a4")
{
    await DrA4ThirdRegistryScenario.RunAsync(options);
}
else if (options.Scenario is "dr-b1")
{
    await DrB1FailoverScenario.RunAsync(options);
}
else if (options.Scenario is "dr-b2")
{
    await DrB2FailoverScenario.RunAsync(options);
}
else if (options.Scenario is "dr-d2")
{
    await DrD2DirectEndpointScenario.RunAsync(options);
}
else if (options.Scenario is "dr-d4")
{
    await DrD4DirectEndpointScenario.RunAsync(options);
}
else if (options.Scenario is "dr-b3")
{
    await DrB3RecoveryScenario.RunAsync(options);
}
else if (options.Scenario is "dr-d1")
{
    await DrD1DirectEndpointScenario.RunAsync(options);
}
else if (options.Scenario is "dr-d3")
{
    await DrD3DirectEndpointScenario.RunAsync(options);
}
else if (options.Scenario is "dr-c1")
{
    await DrC1EmbeddedRegistryScenario.RunAsync(options);
}
else if (options.Scenario is "dr-c2")
{
    await DrC2EmbeddedRegistryScenario.RunAsync(options);
}
else if (options.Scenario is "dr-c3")
{
    await DrC3EmbeddedRegistryScenario.RunAsync(options);
}
else
{
    throw new InvalidOperationException($"Unsupported scenario '{options.Scenario}'.");
}

Console.WriteLine($"discovery-registry-ha client scenario={options.Scenario} result=passed");

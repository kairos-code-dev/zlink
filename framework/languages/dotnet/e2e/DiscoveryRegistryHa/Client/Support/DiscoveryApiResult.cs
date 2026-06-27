namespace DiscoveryRegistryHa.Client.Support;

internal sealed record DiscoveryApiResult(
    string Operation,
    int Reg1TopologyCount,
    int Reg2TopologyCount,
    int Reg3TopologyCount,
    string[] TopologyEvidence)
{
    public string[] ScenarioEvidence { get; init; } = [];
}

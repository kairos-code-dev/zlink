namespace DiscoveryRegistryHa.Client.Support;

internal sealed record DiscoveryApiRes(
    string Operation,
    int Reg1TopologyCount,
    int Reg2TopologyCount,
    int Reg3TopologyCount,
    string[] TopologyEvidence)
{
    public string[] ScenarioEvidence { get; init; } = [];
}
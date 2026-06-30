using DiscoveryRegistryHa.Server.Provider.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.Framework.Contracts.Handlers;

namespace DiscoveryRegistryHa.Server.Provider;

internal sealed class ProfileRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<ProfileReq, ProfileRes>
{
    public ValueTask<ProfileRes> HandleAsync(
        ProfileReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"profile-request|rid={evidence.Rid}|marker={request.Marker}|value={request.Value}");
        return ValueTask.FromResult(new ProfileRes($"profile:{request.Value}", evidence.Rid, request.Marker));
    }
}
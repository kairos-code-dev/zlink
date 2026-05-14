namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkMessageMetadataPolicy(ZLinkFrameworkRegistration registration)
    : IZLinkMessageMetadataPolicy
{
    public bool CanForwardApplicationKey(string key)
    {
        return registration.MetadataPolicy.ForwardedApplicationKeys.Contains(key);
    }
}

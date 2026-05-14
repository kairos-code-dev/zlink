namespace Zlink.Framework.Contracts.Errors;

public sealed class ZLinkConfigurationException : InvalidOperationException
{
    public ZLinkConfigurationException(string message)
        : base(message)
    {
    }
}

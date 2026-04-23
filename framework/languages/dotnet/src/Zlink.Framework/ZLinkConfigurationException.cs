namespace Zlink.Framework;

public sealed class ZLinkConfigurationException : InvalidOperationException
{
    public ZLinkConfigurationException(string message)
        : base(message)
    {
    }
}

namespace Zlink.Framework.Runtime.Streams;

internal interface IZLinkSessionProxyFactory
{
    IZLinkSessionProxy Create(string actorId);
}

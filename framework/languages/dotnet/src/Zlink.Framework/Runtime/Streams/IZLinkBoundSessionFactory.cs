namespace Zlink.Framework.Runtime.Streams;

internal interface IZLinkBoundSessionFactory
{
    IZLinkBoundSession Create(string actorId);
}

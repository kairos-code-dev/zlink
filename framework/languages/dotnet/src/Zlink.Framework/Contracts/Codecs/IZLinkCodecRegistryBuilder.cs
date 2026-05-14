namespace Zlink.Framework.Contracts.Codecs;

public interface IZLinkCodecRegistryBuilder
{
    void AddProtobuf();

    void AddJson();

    void AddMessagePack();
}

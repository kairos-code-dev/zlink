namespace Zlink.Framework.Codecs;

public interface IZLinkCodecRegistryBuilder
{
    void AddProtobuf();

    void AddJson();

    void AddMessagePack();
}

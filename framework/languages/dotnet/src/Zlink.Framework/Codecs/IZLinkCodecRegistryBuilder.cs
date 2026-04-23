namespace Zlink.Framework;

public interface IZLinkCodecRegistryBuilder
{
    void AddProtobuf();

    void AddJson();

    void AddMessagePack();
}

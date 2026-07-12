namespace Zlink.Framework.Contracts.Channels;

/// <summary>
/// Returns the runtime options for a named channel. Changes to properties that
/// support runtime updates take effect immediately. Changing a startup-only
/// property after startup throws an error. Setting a serving channel's weight
/// to zero removes it from new placement while existing work can finish.
/// </summary>
public interface IZLinkChannelRuntimeOptions
{
    IZLinkClientServerChannelOptions ClientServerChannel(string channelName);

    IZLinkRouteMeshChannelOptions RouteMeshChannel(string channelName);
}

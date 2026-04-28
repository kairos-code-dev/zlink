using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Builders;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Compression;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Framing;
using Systems.Zlink.Stream.Connector.Headers;
using Systems.Zlink.Stream.Connector.Metadata;
using Systems.Zlink.Stream.Connector.Options;
using Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.AspNetCore.Builder;

sealed record CreateRoomHttpRequest(string? RoomName);

sealed record CreateRoomHttpReply(string RoomId, string PlayEndpoint, string RoomName);

sealed record CreateRoomCommand(string RoomName) : IZLinkRequest<CreateRoomReply>;

sealed record CreateRoomReply(string RoomId, string PlayEndpoint, string RoomName);

sealed record JoinRoomRequest(string RoomId, string PlayerId) : IZLinkRequest<JoinRoomReply>;

sealed record JoinRoomReply(string RoomId, string PlayerId);

sealed record JoinRoom(string RoomId, string PlayerId);

sealed record JoinRoomAccepted(string RoomId, string PlayerId);

sealed record Echo(string Text);

sealed record EchoReply(string Text);

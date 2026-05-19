namespace Bingo.SessionGateway;

public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(
    string ActorId,
    string DisplayName);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(
    string ActorId,
    string DisplayName);

public sealed record MatchBingoReq(string Mode);

public sealed record MatchBingoRes(
    string RoomId,
    BingoRoomState State);

public sealed record AllocateBingoRoomReq(
    string ActorId,
    string DisplayName,
    string Mode);

public sealed record AllocateBingoRoomRes(
    string RoomId,
    BingoRoomState State);

public sealed record BingoRoomJoinReq(
    string RoomId,
    string ActorId,
    string DisplayName);

public sealed record BingoRoomJoinRes(BingoRoomState State);

public sealed record StartBingoGameReq(string RoomId);

public sealed record StartBingoGameRes(BingoRoomState State);

public sealed record LeaveRoomReq(string RoomId);

public sealed record LeaveRoomRes(BingoRoomState State);

public sealed record PlayerJoinedNotify(
    string RoomId,
    string ActorId,
    string DisplayName,
    int Seat,
    bool IsHost,
    BingoRoomState State);

public sealed record BingoGameStartedNotify(BingoRoomState State);

public sealed record BingoNumberDrawnNotify(
    string RoomId,
    int DrawSeq,
    int Number,
    BingoRoomState State);

public sealed record BingoStateNotify(BingoRoomState State);

public sealed record BingoGameEndedNotify(BingoRoomState State);

public sealed record BingoRoomState(
    string RoomId,
    string Status,
    string HostActorId,
    bool CanStart,
    int DrawSeq,
    int? LastDrawnNumber,
    IReadOnlyList<int> DrawnNumbers,
    IReadOnlyList<BingoPlayerState> Players,
    IReadOnlyList<string> Winners);

public sealed record BingoPlayerState(
    string ActorId,
    string DisplayName,
    int Seat,
    bool IsHost,
    int[] Card,
    bool[] Marks,
    int CompletedLines);

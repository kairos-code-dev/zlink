import {
  GameMarks,
  GameStatus,
  LeaveGameReq,
  ObserveMilestoneReq,
  PacketNames,
  authenticateReq,
  createGameHttpReq,
  joinGameReq,
  placeMarkStreamReq
} from '../Shared/Contracts/messages';
import { BrowserHttpClientFactory } from '../../browser-client-runtime';
import * as connector from '@zlink-systems/stream-connector';
import type {
  AuthenticateRes,
  CreateGameHttpRes,
  GameState,
  GameStateNotify,
  JoinGameRes,
  ObserveMilestoneRes,
  PlaceMarkRes,
  PlayerJoinedNotify,
  WinMilestoneNotify
} from '../Shared/Contracts/messages';
import type { ZlinkStreamConnector, ZlinkStreamMessage } from '@zlink-systems/stream-connector';

class TicTacToeClientScenario {
  async run(apiHttpEndpoint: string, signal?: AbortSignal): Promise<void> {
    const api = BrowserHttpClientFactory.create(apiHttpEndpoint).build();
    let game: CreateGameHttpRes;
    try {
      // 1. Create the room through API.
      game = await api
        .post('/games')
        .body(createGameHttpReq('match-ready'))
        .fetch<CreateGameHttpRes>();
    } finally {
      await api.close();
    }

    ensure(() => game.gameName === 'match-ready');
    ensure(() => game.roomId.length > 0);
    ensure(() => game.ownerPlayEndpoint.length > 0);
    ensure(() => game.playEndpoints.length >= 2);
    ensure(() => new Set(game.playEndpoints).size === game.playEndpoints.length);
    ensure(() => game.playEndpoints.includes(game.ownerPlayEndpoint));
    ensure(() => game.playNodes.length === game.playEndpoints.length);
    ensure(() => new Set(game.playNodes.map((node) => node.spotNodeRid)).size === game.playNodes.length);
    ensure(() => game.playNodes.every((node) => game.playEndpoints.includes(node.streamEndpoint)));
    ensure(() => game.requiredLevel === 3);
    const ownerPlayNode = game.playNodes.find((node) => node.streamEndpoint === game.ownerPlayEndpoint);
    ensure(() => ownerPlayNode !== undefined);
    ensure(() => ownerPlayNode?.spotNodeRid.length !== 0);

    const guestPlayEndpoint = game.playEndpoints.find((endpoint) => endpoint !== game.ownerPlayEndpoint);
    ensure(() => guestPlayEndpoint !== undefined);
    const observerPlayEndpoint = guestPlayEndpoint as string;
    const observerPlayNode = game.playNodes.find((node) => node.streamEndpoint === observerPlayEndpoint);
    ensure(() => observerPlayNode !== undefined);

    const client1 = createPlayerClient(game.ownerPlayEndpoint, 'host');
    const client2 = createPlayerClient(observerPlayEndpoint, 'guest');
    const observer = createPlayerClient(observerPlayEndpoint, 'observer');

    try {
      // 2. Host, guest, and observer connect directly to Play stream endpoints from the API response.
      await client1.connect(signal);
      const client1Auth = await client1.request(authenticateReq('player-x')).submit<AuthenticateRes>(signal);
      ensure(() => client1Auth.player.actorId === 'player-x');
      ensure(() => client1Auth.player.displayName.length > 0);
      ensure(() => client1Auth.player.level >= game.requiredLevel);
      ensure(() => client1Auth.player.wins === 99);

      await client2.connect(signal);
      const client2Auth = await client2.request(authenticateReq('player-o')).submit<AuthenticateRes>(signal);
      ensure(() => client2Auth.player.actorId === 'player-o');
      ensure(() => client2Auth.player.actorId !== client1Auth.player.actorId);
      ensure(() => client2Auth.player.displayName.length > 0);
      ensure(() => client2Auth.player.level >= game.requiredLevel);
      ensure(() => client2Auth.player.wins >= 0);

      await observer.connect(signal);
      const observerAuth = await observer.request(authenticateReq('observer')).submit<AuthenticateRes>(signal);
      ensure(() => observerAuth.player.actorId === 'observer');
      ensure(() => observerAuth.player.displayName.length > 0);
      ensure(() => observerAuth.player.level >= game.requiredLevel);
      ensure(() => observerAuth.player.wins === 0);
      const observerSubscription = await observer
        .request(new ObserveMilestoneReq())
        .submit<ObserveMilestoneRes>(signal);
      ensure(() => observerSubscription.subscribed);
      console.log('observer-subscription=verified');

      // 3. Host joins by explicit RoomId
      const client1Join = await client1.request(joinGameReq(game.roomId)).submit<JoinGameRes>(signal);
      ensure(() => stateOf(client1Join).roomId === game.roomId);
      ensure(() => stateOf(client1Join).status === GameStatus.WaitingForPlayers);
      ensure(() => stateOf(client1Join).xActorId === client1Auth.player.actorId);
      ensure(() => stateOf(client1Join).oActorId === null);
      ensure(() => stateOf(client1Join).board === '.........');
      await expectNoMessage(
        client1,
        PacketNames.playerJoinedNotify,
        (message: ZlinkStreamMessage<PlayerJoinedNotify>) => message.payload.actorId === client1Auth.player.actorId,
        signal
      );

      const client1SawClient2Join = client1
        .waitFor<PlayerJoinedNotify>(PacketNames.playerJoinedNotify)
        .where((message) => message.payload.actorId === client2Auth.player.actorId)
        .submit(signal);
      const client1RunningState = client1
        .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
        .where((message) => message.payload.state.status === GameStatus.InProgress)
        .submit(signal);

      // 4-6. Guest joins by the same RoomId.
      const client2Join = await client2.request(joinGameReq(game.roomId)).submit<JoinGameRes>(signal);
      ensure(() => stateOf(client2Join).roomId === game.roomId);
      ensure(() => stateOf(client2Join).status === GameStatus.InProgress);
      ensure(() => stateOf(client2Join).oActorId === client2Auth.player.actorId);
      ensure(() => stateOf(client2Join).xActorId === client1Auth.player.actorId);
      ensure(() => stateOf(client2Join).nextTurn === GameMarks.x);
      await expectNoMessage(
        client2,
        PacketNames.playerJoinedNotify,
        (message: ZlinkStreamMessage<PlayerJoinedNotify>) => message.payload.actorId === client2Auth.player.actorId,
        signal
      );

      const [client1Joined, client1Running] = await Promise.all([
        client1SawClient2Join,
        client1RunningState
      ]);
      ensure(() => client1Joined.payload.roomId === game.roomId);
      ensure(() => client1Joined.payload.actorId === client2Auth.player.actorId);
      ensure(() => client1Joined.payload.displayName === client2Auth.player.displayName);
      ensure(() => client1Joined.payload.level === client2Auth.player.level);
      ensure(() => client1Joined.payload.mark === GameMarks.o);
      ensure(() => stateOf(client1Joined.payload).status === GameStatus.InProgress);
      ensure(() => client1Running.payload.state.nextTurn === GameMarks.x);

      // 7. Each move response is matched with the opponent notify.
      const client2SawMove1 = waitState(client2, 0, signal);
      const client1Move1 = await client1.request(placeMarkStreamReq(0)).submit<PlaceMarkRes>(signal);
      requireSameState(stateOf(client1Move1), (await client2SawMove1).payload.state);
      ensure(() => stateOf(client1Move1).board === 'X........');
      ensure(() => stateOf(client1Move1).nextTurn === GameMarks.o);
      requireLastMove(stateOf(client1Move1), client1Auth.player.actorId, 0);

      const client1SawMove2 = waitState(client1, 3, signal);
      const client2Move1 = await client2.request(placeMarkStreamReq(3)).submit<PlaceMarkRes>(signal);
      requireSameState(stateOf(client2Move1), (await client1SawMove2).payload.state);
      ensure(() => stateOf(client2Move1).board === 'X..O.....');
      ensure(() => stateOf(client2Move1).nextTurn === GameMarks.x);
      requireLastMove(stateOf(client2Move1), client2Auth.player.actorId, 3);

      const client2SawMove3 = waitState(client2, 1, signal);
      const client1Move2 = await client1.request(placeMarkStreamReq(1)).submit<PlaceMarkRes>(signal);
      requireSameState(stateOf(client1Move2), (await client2SawMove3).payload.state);
      ensure(() => stateOf(client1Move2).board === 'XX.O.....');
      ensure(() => stateOf(client1Move2).nextTurn === GameMarks.o);
      requireLastMove(stateOf(client1Move2), client1Auth.player.actorId, 1);

      const client1SawMove4 = waitState(client1, 4, signal);
      const client2Move2 = await client2.request(placeMarkStreamReq(4)).submit<PlaceMarkRes>(signal);
      requireSameState(stateOf(client2Move2), (await client1SawMove4).payload.state);
      ensure(() => stateOf(client2Move2).board === 'XX.OO....');
      ensure(() => stateOf(client2Move2).nextTurn === GameMarks.x);
      requireLastMove(stateOf(client2Move2), client2Auth.player.actorId, 4);

      const client2SawFinalMove = client2
        .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
        .where((message) =>
          message.payload.state.roomId === game.roomId &&
          message.payload.state.status === GameStatus.Won &&
          message.payload.state.winner === client1Auth.player.actorId)
        .submit(signal);
      const observerSawMilestone = observer
        .waitFor<WinMilestoneNotify>(PacketNames.winMilestoneNotify)
        .where((message) =>
          message.payload.roomId === game.roomId &&
          message.payload.actorId === client1Auth.player.actorId)
        .submit(signal);
      // 8. The final host move wins and publishes the observer milestone.
      const client1FinalMove = await client1.request(placeMarkStreamReq(2)).submit<PlaceMarkRes>(signal);
      requireSameState(stateOf(client1FinalMove), (await client2SawFinalMove).payload.state);
      ensure(() => stateOf(client1FinalMove).board === 'XXXOO....');
      ensure(() => stateOf(client1FinalMove).status === GameStatus.Won);
      ensure(() => stateOf(client1FinalMove).winner === client1Auth.player.actorId);
      ensure(() => stateOf(client1FinalMove).nextTurn === '');
      requireLastMove(stateOf(client1FinalMove), client1Auth.player.actorId, 2);

      const milestone = await observerSawMilestone;
      ensure(() => milestone.payload.roomId === game.roomId);
      ensure(() => milestone.payload.actorId === client1Auth.player.actorId);
      ensure(() => milestone.payload.displayName === client1Auth.player.displayName);
      ensure(() => milestone.payload.wins === 100);
      ensure(() => milestone.payload.receivingSpotNodeRid === observerPlayNode?.spotNodeRid);
      console.log(
        `observer-win-milestone=verified actor=${milestone.payload.actorId} ` +
        `wins=${milestone.payload.wins} receivingSpotNodeRid=${milestone.payload.receivingSpotNodeRid}`
      );

      await Promise.all([
        client1.send(new LeaveGameReq(game.roomId)).packetName(PacketNames.leaveGameReq).submit(),
        client2.send(new LeaveGameReq(game.roomId)).packetName(PacketNames.leaveGameReq).submit()
      ]);
    } finally {
      await Promise.allSettled([client1.close(), client2.close(), observer.close()]);
    }
  }
}

function waitState(
  client: ZlinkStreamConnector,
  lastMoveCell: number,
  signal?: AbortSignal
): Promise<ZlinkStreamMessage<GameStateNotify>> {
  return client
    .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
    .where((message) => message.payload.state.lastMoveCell === lastMoveCell)
    .submit(signal);
}

async function expectNoMessage<TPayload>(
  client: ZlinkStreamConnector,
  packetName: string,
  predicate: (message: ZlinkStreamMessage<TPayload>) => boolean,
  signal?: AbortSignal
): Promise<void> {
  try {
    await client
      .waitFor<TPayload>(packetName)
      .where(predicate)
      .timeout(25)
      .submit(signal);
  } catch (error) {
    if (isRequestTimeout(error)) {
      return;
    }
    throw error;
  }
  throw new Error(`Unexpected stream message '${packetName}'.`);
}

function isRequestTimeout(error: unknown): boolean {
  return typeof error === 'object'
    && error !== null
    && 'error' in error
    && (error as { error?: { code?: unknown } }).error?.code === 'requestTimeout';
}

function createPlayerClient(endpoint: string, name: string): ZlinkStreamConnector {
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    waitTimeoutMs: 60000,
    heartbeat: { enabled: false }
  });
  client.observeInbound((observation) => {
    console.log(
      `stream-inbound sample=TicTacToe client=${name} kind=${observation.kind} ` +
      `name=${observation.name} seq=${observation.requestSeq?.toString() ?? '-'} ` +
      `bytes=${observation.payloadLength}`
    );
  });
  return client;
}

function stateOf(message: { state: GameState }): GameState {
  return message.state;
}

function requireLastMove(state: GameState, actorId: string, cell: number): void {
  ensure(() => state.lastMoveActorId === actorId);
  ensure(() => state.lastMoveCell === cell);
}

function requireSameState(actual: GameState, expected: GameState): void {
  ensure(() => actual.roomId === expected.roomId);
  ensure(() => actual.board === expected.board);
  ensure(() => actual.status === expected.status);
  ensure(() => actual.winner === expected.winner);
  ensure(() => actual.nextTurn === expected.nextTurn);
  ensure(() => actual.lastMoveActorId === expected.lastMoveActorId);
  ensure(() => actual.lastMoveCell === expected.lastMoveCell);
}

function ensure(condition: () => boolean): void {
  if (!condition()) {
    throw new Error(`Ensure failed: ${conditionExpression(condition)}`);
  }
}

function conditionExpression(condition: () => boolean): string {
  return condition
    .toString()
    .replace(/^\s*\(\)\s*=>\s*/, '')
    .replace(/^\s*function\s*\(\)\s*\{\s*return\s*/, '')
    .replace(/;?\s*\}\s*$/, '')
    .trim();
}

export { TicTacToeClientScenario };

import { GameMarks, GameStatus, PacketNames, authenticateReq, createGameReq, joinGameReq, placeMarkStreamReq } from '../Shared/Contracts/messages';
import * as connector from '@zlink-systems/stream-connector';
import * as json from '@zlink-systems/stream-connector-json';
import { SampleTimings } from './Configuration/sample-settings';
import type {
  AuthenticateRes,
  CreateGameHttpRes,
  GameState,
  GameStateNotify,
  JoinGameRes,
  PlaceMarkRes,
  PlayerJoinedNotify
} from '../Shared/Contracts/messages';
import type { ZlinkStreamConnector, ZlinkStreamMessage } from '@zlink-systems/stream-connector';

class TicTacToeClientScenario {
  async run(
    apiHttpEndpoint: string,
    signal?: AbortSignal
  ): Promise<void> {
    // 1. Create the room through API and verify the returned game name, room id, and Play endpoint.
    const createGameResponse = await fetch(`${apiHttpEndpoint}/games`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(createGameReq('match-ready')),
      signal
    });

    ensure(() => createGameResponse.ok);
    const game = await createGameResponse.json() as CreateGameHttpRes;

    ensure(() => game.gameName === 'match-ready');
    ensure(() => game.roomId.length > 0);
    ensure(() => game.playEndpoint.length > 0);
    ensure(() => !/^[0-9a-f]{32}$/i.test(game.roomId));

    const client1 = createPlayerClient(game.playEndpoint);
    const client2 = createPlayerClient(game.playEndpoint);

    try {
      // 2. Both clients connect directly to the Play stream endpoint and authenticate there.
      await client1.connect(signal);
      await client2.connect(signal);

      const client1Auth = await client1.request(authenticateReq('p1')).submit<AuthenticateRes>(signal);
      const client2Auth = await client2.request(authenticateReq('p2')).submit<AuthenticateRes>(signal);

      ensure(() => client1Auth.actorId === 'p1');
      ensure(() => client2Auth.actorId === 'p2');
      ensure(() => client1Auth.displayName === 'Player X');
      ensure(() => client2Auth.displayName === 'Player O');

      // 3. Host joins by explicit RoomId, gets X/WaitingForPlayers, and receives no self-join notify.
      const client1SelfJoinNotify = expectNoMessage(
        client1,
        PacketNames.playerJoinedNotify,
        (message: ZlinkStreamMessage<PlayerJoinedNotify>) => message.payload.actorId === client1Auth.actorId,
        signal
      );
      const client1Join = await client1.request(joinGameReq(game.roomId)).submit<JoinGameRes>(signal);

      ensure(() => client1Join.roomId === game.roomId);
      ensure(() => stateOf(client1Join).roomId === game.roomId);
      ensure(() => client1Join.actorId === client1Auth.actorId);
      ensure(() => client1Join.mark === GameMarks.x);
      ensure(() => stateOf(client1Join).xActorId === client1Auth.actorId);
      ensure(() => stateOf(client1Join).oActorId === null);
      ensure(() => stateOf(client1Join).status === GameStatus.WaitingForPlayers);
      await client1SelfJoinNotify;

      // 4-6. Guest joins by the same RoomId; host receives join and running-state pushes, guest receives no self-join notify.
      const client1SawClient2Join = client1
        .waitFor<PlayerJoinedNotify>(PacketNames.playerJoinedNotify)
        .where((message) => message.payload.actorId === client2Auth.actorId)
        .submit(signal);
      const client2SelfJoinNotify = expectNoMessage(
        client2,
        PacketNames.playerJoinedNotify,
        (message: ZlinkStreamMessage<PlayerJoinedNotify>) => message.payload.actorId === client2Auth.actorId,
        signal
      );
      const client1RunningState = client1
        .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
        .where((message) => message.payload.state.status === GameStatus.InProgress)
        .submit(signal);

      const client2Join = await client2.request(joinGameReq(game.roomId)).submit<JoinGameRes>(signal);

      ensure(() => client2Join.roomId === game.roomId);
      ensure(() => stateOf(client2Join).roomId === game.roomId);
      ensure(() => client2Join.actorId === client2Auth.actorId);
      ensure(() => client2Join.mark === GameMarks.o);
      ensure(() => stateOf(client2Join).xActorId === client1Auth.actorId);
      ensure(() => stateOf(client2Join).oActorId === client2Auth.actorId);
      ensure(() => stateOf(client2Join).status === GameStatus.InProgress);
      await client2SelfJoinNotify;

      const [client1Joined, client1Running] = await Promise.all([
        client1SawClient2Join,
        client1RunningState
      ]);

      ensure(() => client1Joined.payload.roomId === game.roomId);
      ensure(() => client1Joined.payload.actorId === client2Auth.actorId);
      ensure(() => client1Joined.payload.mark === GameMarks.o);
      ensure(() => stateOf(client1Joined.payload).roomId === game.roomId);
      ensure(() => stateOf(client1Joined.payload).status === GameStatus.InProgress);
      ensure(() => client1Running.payload.state.status === GameStatus.InProgress);
      ensure(() => client1Running.payload.state.nextTurn === client1Auth.actorId);

      // 7. Each move response and opponent notify carry the same board, turn, and last-move state.
      const client2SawMove1 = client2
        .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
        .where((message) => message.payload.state.lastMoveCell === 0)
        .submit(signal);
      const client1Move1 = await client1.request(placeMarkStreamReq(0)).submit<PlaceMarkRes>(signal);
      const client2Move1 = await client2SawMove1;

      requireSameState(stateOf(client1Move1), client2Move1.payload.state);
      ensure(() => stateOf(client1Move1).board === 'X........');
      ensure(() => stateOf(client1Move1).nextTurn === client2Auth.actorId);
      ensure(() => stateOf(client1Move1).lastMoveActorId === client1Auth.actorId);
      ensure(() => stateOf(client1Move1).lastMoveCell === 0);

      const client1SawMove2 = client1
        .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
        .where((message) => message.payload.state.lastMoveCell === 3)
        .submit(signal);
      const client2Move2 = await client2.request(placeMarkStreamReq(3)).submit<PlaceMarkRes>(signal);
      const client1Move2 = await client1SawMove2;

      requireSameState(stateOf(client2Move2), client1Move2.payload.state);
      ensure(() => stateOf(client2Move2).board === 'X..O.....');
      ensure(() => stateOf(client2Move2).nextTurn === client1Auth.actorId);
      ensure(() => stateOf(client2Move2).lastMoveActorId === client2Auth.actorId);
      ensure(() => stateOf(client2Move2).lastMoveCell === 3);

      const client2SawMove3 = client2
        .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
        .where((message) => message.payload.state.lastMoveCell === 1)
        .submit(signal);
      const client1Move3 = await client1.request(placeMarkStreamReq(1)).submit<PlaceMarkRes>(signal);
      const client2Move3 = await client2SawMove3;

      requireSameState(stateOf(client1Move3), client2Move3.payload.state);
      ensure(() => stateOf(client1Move3).board === 'XX.O.....');
      ensure(() => stateOf(client1Move3).nextTurn === client2Auth.actorId);
      ensure(() => stateOf(client1Move3).lastMoveActorId === client1Auth.actorId);
      ensure(() => stateOf(client1Move3).lastMoveCell === 1);

      const client1SawMove4 = client1
        .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
        .where((message) => message.payload.state.lastMoveCell === 4)
        .submit(signal);
      const client2Move4 = await client2.request(placeMarkStreamReq(4)).submit<PlaceMarkRes>(signal);
      const client1Move4 = await client1SawMove4;

      requireSameState(stateOf(client2Move4), client1Move4.payload.state);
      ensure(() => stateOf(client2Move4).board === 'XX.OO....');
      ensure(() => stateOf(client2Move4).nextTurn === client1Auth.actorId);
      ensure(() => stateOf(client2Move4).lastMoveActorId === client2Auth.actorId);
      ensure(() => stateOf(client2Move4).lastMoveCell === 4);

      // 8. The final host move wins; requester gets PlaceMarkRes and guest gets the same final notify.
      const client2SawFinalMove = client2
        .waitFor<GameStateNotify>(PacketNames.gameStateNotify)
        .where((message) => message.payload.state.winner === client1Auth.actorId)
        .submit(signal);
      const client1FinalMove = await client1.request(placeMarkStreamReq(2)).submit<PlaceMarkRes>(signal);
      const client2FinalMove = await client2SawFinalMove;

      requireSameState(stateOf(client1FinalMove), client2FinalMove.payload.state);
      ensure(() => stateOf(client1FinalMove).board === 'XXXOO....');
      ensure(() => stateOf(client1FinalMove).status === GameStatus.Won);
      ensure(() => stateOf(client1FinalMove).winner === client1Auth.actorId);
      ensure(() => stateOf(client1FinalMove).lastMoveActorId === client1Auth.actorId);
      ensure(() => stateOf(client1FinalMove).lastMoveCell === 2);
      ensure(() => stateOf(client1FinalMove).nextTurn === null);
    } finally {
      await Promise.allSettled([client1.close(), client2.close()]);
    }
  }
}

function createPlayerClient(endpoint: string): ZlinkStreamConnector {
  return connector.zlinkStreamConnectorFactory.create({
    endpoint,
    codec: json.zlinkStreamJsonCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
}

function stateOf(message: { state: unknown }): GameState {
  return message.state as GameState;
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

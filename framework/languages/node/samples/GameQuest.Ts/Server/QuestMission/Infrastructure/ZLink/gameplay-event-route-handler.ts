import { zlinkSendHandler } from '@zlink-systems/nestjs';
import { GameplayMsg, PacketNames } from '../../../../Shared/Contracts/messages';
import { PlayerQuestSpotProvisioner } from './player-quest-spot-provisioner';
import type { ZLinkSendHandler } from '@zlink-systems/framework';

@zlinkSendHandler('quest-owner', PacketNames.gameplayMsg)
class GameplayEventRouteHandler implements ZLinkSendHandler<GameplayMsg> {
  constructor(
    private readonly playerQuests: PlayerQuestSpotProvisioner
  ) {}

  async handle(message: GameplayMsg): Promise<void> {
    await this.playerQuests.send(
      message.playerId,
      new GameplayMsg(message.eventId, message.playerId, message.type, [...message.payload], message.occurredAtUnixMs)
    );
  }
}

export { GameplayEventRouteHandler };

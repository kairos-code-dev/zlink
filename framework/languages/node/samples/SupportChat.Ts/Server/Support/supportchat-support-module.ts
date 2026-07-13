import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { createSupportChatLocationStore, supportChatLocationOptions } from '../Configuration/location-store';
import { AgentAssignmentService } from './Application/ConversationAssignment/agent-assignment-service';
import { AgentAvailabilityDirectory } from './Application/ConversationAssignment/agent-availability-directory';
import { SupportConversationAllocator } from './Application/ConversationAssignment/support-conversation-allocator';
import { SupportActorDirectory } from './Infrastructure/ZLink/Actors/support-actor-directory';
import { SupportUserActorFactory } from './Infrastructure/ZLink/Actors/support-user-actor-factory';
import { AllocateConversationHandler } from './Infrastructure/ZLink/Handlers/allocate-conversation-handler';
import { EnsureAgentConversationHandler } from './Infrastructure/ZLink/Handlers/ensure-agent-conversation-handler';
import { EnsureSupportUserActorHandler } from './Infrastructure/ZLink/Handlers/ensure-support-user-actor-handler';
import { ConversationIdleTimerHandler } from './Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation-idle-timer-handler';
import {
  CloseConversationHandler,
  EnsureConversationAssignmentHandler,
  JoinConversationHandler,
  SendChatMessageHandler,
  SetTypingHandler
} from './Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation-actor-handlers';
import { ConversationSpot } from './Infrastructure/ZLink/Spots/ConversationSpot/conversation-spot';
import { SupportNotificationPublisher } from './Infrastructure/ZLink/Spots/ConversationSpot/Notifications/support-notification-publisher';
import {
  OpenConversationActorHandler,
  SetAgentAvailableHandler
} from './Infrastructure/ZLink/Spots/EntrySpot/support-entry-handlers';
import { SupportEntrySpot } from './Infrastructure/ZLink/Spots/EntrySpot/support-entry-spot';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatSupportModule(config: SupportChatServerConfig) {
  class SupportChatSupportModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SUPPORTCHAT_LOG_DIR ?? 'logs'}/flow-support.log`)
            .traceLabel('support');
          builder.addLocationStore(createSupportChatLocationStore(config));
          Object.assign(builder.configureLocations(), supportChatLocationOptions());
          return builder
            .addClientServerChannel(SampleNames.apiChannel)
              .enableClient()
            .addClientServerChannel(SampleNames.supportChannel)
              .enableServer(config.supportChannelEndpoint)
              .addHandlerGroup('support')
            .addSpotMesh(SampleNames.conversationSpotMesh)
              .enableRouter(config.supportSpotEndpoint, 'support-node')
              .addEntrySpot(SupportEntrySpot)
              .addSpotFactory(ConversationSpot)
              .actorFactory('support.user', SupportUserActorFactory)
            .build();
        }
      })
    ],
    providers: [
      AgentAvailabilityDirectory,
      AgentAssignmentService,
      SupportConversationAllocator,
      SupportActorDirectory,
      SupportUserActorFactory,
      SupportEntrySpot,
      EnsureSupportUserActorHandler,
      EnsureAgentConversationHandler,
      AllocateConversationHandler,
      SetAgentAvailableHandler,
      OpenConversationActorHandler,
      JoinConversationHandler,
      SendChatMessageHandler,
      SetTypingHandler,
      CloseConversationHandler,
      ConversationIdleTimerHandler,
      EnsureConversationAssignmentHandler,
      SupportNotificationPublisher
    ]
  })(SupportChatSupportModule);

  return SupportChatSupportModule;
}

export { createSupportChatSupportModule };

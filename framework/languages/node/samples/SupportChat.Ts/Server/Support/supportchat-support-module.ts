import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { SupportNotificationDeliveryLog } from './notification-delivery-log';
import { SupportUserActorFactory } from './Infrastructure/ZLink/Actors/support-user-actor-factory';
import { ConversationEventMapper } from './Infrastructure/ZLink/Notifications/conversation-event-mapper';
import { SupportNotificationPublisher } from './Infrastructure/ZLink/Notifications/support-notification-publisher';
import { SupportEntrySpot } from './Infrastructure/ZLink/Spots/support-entry-spot';
import { ConversationSpot } from './Infrastructure/ZLink/Spots/conversation-spot';
import {
  CONVERSATION_EXECUTOR,
  CONVERSATION_STARTER,
  SupportConversationAllocator
} from './Application/ConversationAssignment/support-conversation-allocator';
import { ZLinkConversationSpotAccess } from './Infrastructure/ZLink/conversation-spot-access';
import { AgentAvailabilityDirectory } from './Application/ConversationAssignment/agent-availability-directory';
import { AgentAssignmentService } from './Application/ConversationAssignment/agent-assignment-service';
import { SampleNames } from '../Configuration/sample-names';
function createSupportChatSupportModule(config: {
  registryRouterEndpoint: string;
  notificationEndpoint: string;
  supportEndpoint: string;
  apiEndpoint: string;
}) {
  class SupportChatSupportModule {}

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.supportChannel)
            .enableServer(config.supportEndpoint)
            .addHandlerGroup('support')
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.notificationChannel)
            .enableServer(config.notificationEndpoint)
            .addHandlerGroup('notifications')
          .actorFactory(SampleNames.supportActorType, SupportUserActorFactory)
          .addSpotNode(SampleNames.conversationSpotType)
            .addEntrySpot(SupportEntrySpot)
            .addSpotFactory(ConversationSpot)
          .build()
      })
    ],
    providers: [
      SupportNotificationDeliveryLog,
      SupportUserActorFactory,
      ConversationEventMapper,
      SupportNotificationPublisher,
      ZLinkConversationSpotAccess,
      { provide: CONVERSATION_STARTER, useExisting: ZLinkConversationSpotAccess },
      { provide: CONVERSATION_EXECUTOR, useExisting: ZLinkConversationSpotAccess },
      SupportConversationAllocator,
      AgentAvailabilityDirectory,
      AgentAssignmentService,
      SupportEntrySpot
    ]
  })(SupportChatSupportModule);

  return SupportChatSupportModule;
}

export { createSupportChatSupportModule };

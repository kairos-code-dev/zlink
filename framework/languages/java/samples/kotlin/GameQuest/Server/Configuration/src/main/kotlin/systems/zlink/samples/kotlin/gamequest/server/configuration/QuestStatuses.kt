package systems.zlink.samples.kotlin.gamequest.server.configuration

/** Quest aggregate status values carried by `QuestProgress.status`. */
object QuestStatuses {
    const val Active = "Active"
    const val Completed = "Completed"
    const val RewardGranted = "RewardGranted"
}

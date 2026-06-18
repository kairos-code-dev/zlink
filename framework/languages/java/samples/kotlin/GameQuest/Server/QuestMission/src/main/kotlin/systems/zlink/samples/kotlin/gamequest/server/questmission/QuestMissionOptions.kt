package systems.zlink.samples.kotlin.gamequest.server.questmission

/** Parsed `--mission` selector for a QuestMission process (`mission-a`/`mission-b`). */
data class QuestMissionOptions(val missionName: String) {
    companion object {
        fun fromArgs(args: Array<String>): QuestMissionOptions {
            var missionName = "mission-a"
            for (i in 0 until args.size - 1) {
                if (args[i] == "--mission") {
                    missionName = args[i + 1]
                }
            }
            return QuestMissionOptions(missionName)
        }
    }
}

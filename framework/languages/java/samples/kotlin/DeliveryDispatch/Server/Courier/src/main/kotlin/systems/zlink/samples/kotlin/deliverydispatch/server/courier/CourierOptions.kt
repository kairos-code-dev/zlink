package systems.zlink.samples.kotlin.deliverydispatch.server.courier

data class CourierOptions(val courierId: String, val mode: String) {
    companion object {
        fun fromArgs(args: Array<String>): CourierOptions {
            val courierId = readOption(args, "--courier")
                ?: System.getenv("DELIVERYDISPATCH_COURIER_ID")
                ?: "courier-a"
            val mode = readOption(args, "--mode")
                ?: System.getenv("DELIVERYDISPATCH_COURIER_MODE")
                ?: "accept"
            return CourierOptions(courierId, mode)
        }

        private fun readOption(args: Array<String>, name: String): String? {
            var index = 0
            while (index + 1 < args.size) {
                if (args[index] == name) {
                    return args[index + 1]
                }
                index++
            }
            return null
        }
    }
}

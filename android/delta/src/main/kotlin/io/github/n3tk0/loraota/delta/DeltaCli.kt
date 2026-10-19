package io.github.n3tk0.loraota.delta

import java.io.File
import kotlin.system.exitProcess

/** Makes a delta on a computer, the same one the app makes: `old.bin new.bin out.patch`. */
fun main(args: Array<String>) {
    if (args.size != 3) {
        System.err.println("usage: old.bin new.bin out.patch")
        exitProcess(2)
    }
    val old = File(args[0]).readBytes()
    val new = File(args[1]).readBytes()
    val patch = DeltaGenerator.create(old, new)
    File(args[2]).writeBytes(patch)
    println("delta ${patch.size} bytes for ${new.size} (old ${old.size})")
}

package io.github.n3tk0.loraota.backup

/** A setting, channel or contact that is in only one of two compared backups. */
data class CompareEntry(val id: String, val label: String, val value: String, val secret: Boolean = false)

/** A setting or channel in both backups with another value; secrets keep [secret] set and are shown redacted. */
data class CompareChange(val id: String, val label: String, val old: String, val new: String, val secret: Boolean = false)

/**
 * What differs between two backups, old to new: settings added, removed and changed; channels added,
 * removed and changed; and how many contacts each has. Values of secrets (passwords, private keys, a
 * channel's secret) are never part of the result: they show as [REDACTED] and only say that they differ.
 */
class BackupCompare(
    val settingsAdded: List<CompareEntry>,
    val settingsRemoved: List<CompareEntry>,
    val settingsChanged: List<CompareChange>,
    val channelsAdded: List<CompareEntry>,
    val channelsRemoved: List<CompareEntry>,
    val channelsChanged: List<CompareChange>,
    val contactsOld: Int,
    val contactsNew: Int,
    val contactsAdded: List<String>,
    val contactsRemoved: List<String>,
) {
    val contactsDelta get() = contactsNew - contactsOld

    val isEmpty
        get() = settingsAdded.isEmpty() && settingsRemoved.isEmpty() && settingsChanged.isEmpty() &&
            channelsAdded.isEmpty() && channelsRemoved.isEmpty() && channelsChanged.isEmpty() &&
            contactsAdded.isEmpty() && contactsRemoved.isEmpty() && contactsOld == contactsNew

    companion object {
        const val REDACTED = "••••"

        private val SECRET_WORDS = listOf("password", "passwd", "pwd", "secret", "private", "prv")
        private val SECRET_IDS = setOf("pin")

        /** A setting whose value must not be shown: passwords, private keys, secrets, the pin. */
        fun isSecret(id: String): Boolean {
            val k = id.lowercase()
            return k in SECRET_IDS || id in PasswordBox.SECRET_SETTINGS || SECRET_WORDS.any { it in k }
        }

        /** A channel's value is hex of its name (32 bytes) and its secret (16 bytes): the secret is the tail. */
        private fun channelSecret(v: String) = if (v.length > 64) v.substring(64) else ""

        fun compare(old: Backup, new: Backup): BackupCompare {
            val oldS = old.items.filter { it.group == BackupItem.GROUP_SETTING }.associateBy { it.id }
            val newS = new.items.filter { it.group == BackupItem.GROUP_SETTING }.associateBy { it.id }
            fun entry(i: BackupItem) = CompareEntry(i.id, i.id, if (isSecret(i.id)) REDACTED else i.value, isSecret(i.id))
            val settingsAdded = newS.keys.filter { it !in oldS }.sorted().map { entry(newS.getValue(it)) }
            val settingsRemoved = oldS.keys.filter { it !in newS }.sorted().map { entry(oldS.getValue(it)) }
            val settingsChanged = oldS.keys.filter { it in newS && oldS.getValue(it).value != newS.getValue(it).value }.sorted().map {
                val secret = isSecret(it)
                CompareChange(it, it, if (secret) REDACTED else oldS.getValue(it).value,
                    if (secret) REDACTED else newS.getValue(it).value, secret)
            }

            val oldC = old.items.filter { it.group == BackupItem.GROUP_CHANNEL }.associateBy { it.id }
            val newC = new.items.filter { it.group == BackupItem.GROUP_CHANNEL }.associateBy { it.id }
            fun slotOrder(ids: Collection<String>) = ids.sortedWith(compareBy({ it.toIntOrNull() ?: Int.MAX_VALUE }, { it }))
            fun chName(i: BackupItem) = i.label.ifEmpty { "#${i.id}" }
            val channelsAdded = slotOrder(newC.keys.filter { it !in oldC }).map { CompareEntry(it, chName(newC.getValue(it)), REDACTED, true) }
            val channelsRemoved = slotOrder(oldC.keys.filter { it !in newC }).map { CompareEntry(it, chName(oldC.getValue(it)), REDACTED, true) }
            val channelsChanged = slotOrder(oldC.keys.filter { it in newC && oldC.getValue(it).value != newC.getValue(it).value }).map {
                val a = oldC.getValue(it)
                val b = newC.getValue(it)
                val secretChanged = channelSecret(a.value) != channelSecret(b.value)
                CompareChange(it, "#$it", chName(a), chName(b), secretChanged)
            }

            val oldK = old.items.filter { it.group == BackupItem.GROUP_CONTACT }.associateBy { it.id }
            val newK = new.items.filter { it.group == BackupItem.GROUP_CONTACT }.associateBy { it.id }
            fun cname(i: BackupItem) = i.label.ifEmpty { i.id.take(8) }
            return BackupCompare(
                settingsAdded, settingsRemoved, settingsChanged,
                channelsAdded, channelsRemoved, channelsChanged,
                oldK.size, newK.size,
                newK.filterKeys { it !in oldK }.values.map(::cname).sorted(),
                oldK.filterKeys { it !in newK }.values.map(::cname).sorted(),
            )
        }

        /** The older of two backups first, so that "old to new" reads in time order. */
        fun inTimeOrder(a: Backup, b: Backup): Pair<Backup, Backup> = if (a.time <= b.time) a to b else b to a
    }
}

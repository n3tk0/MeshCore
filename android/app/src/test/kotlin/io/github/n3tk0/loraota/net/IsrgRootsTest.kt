package io.github.n3tk0.loraota.net

import java.io.File
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.util.Date
import javax.security.auth.x500.X500Principal
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/** The ISRG roots bundled for Android 7.0 (no ISRG roots in its store) and wired into the network security config. */
class IsrgRootsTest {
    private val roots = mapOf(
        "isrg_root_x1" to "ISRG Root X1",
        "isrg_root_x2" to "ISRG Root X2",
        "isrg_root_yr" to "Root YR",
        "isrg_root_ye" to "Root YE",
    )

    private fun load(name: String): X509Certificate =
        File("src/main/res/raw/$name.pem").inputStream().use {
            CertificateFactory.getInstance("X.509").generateCertificate(it) as X509Certificate
        }

    private fun cn(p: X500Principal) =
        Regex("CN=([^,]+)").find(p.getName(X500Principal.RFC2253))!!.groupValues[1]

    @Test fun rootsAreSelfSignedValidAndNamedAsExpected() {
        for ((file, name) in roots) {
            val c = load(file)
            assertEquals(name, cn(c.subjectX500Principal), file)
            assertEquals(c.subjectX500Principal, c.issuerX500Principal, "$file self-issued")
            c.verify(c.publicKey)   // self-signature
            c.checkValidity(Date())
        }
    }

    @Test fun networkSecurityConfigTrustsSystemAndEveryRoot() {
        val xml = File("src/main/res/xml/network_security_config.xml").readText()
        assertTrue("""src="system"""" in xml)
        for (file in roots.keys) assertTrue("@raw/$file" in xml, file)
        assertTrue("""cleartextTrafficPermitted="false"""" in xml)
        assertTrue("@xml/network_security_config" in File("src/main/AndroidManifest.xml").readText())
    }
}

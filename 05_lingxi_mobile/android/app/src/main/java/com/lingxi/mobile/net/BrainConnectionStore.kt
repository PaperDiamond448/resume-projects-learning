package com.lingxi.mobile.net

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import com.lingxi.mobile.BuildConfig
import java.security.KeyStore
import java.security.MessageDigest
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Endpoint + bearer credential storage.
 *
 * The endpoint is not secret. The bearer token is encrypted with a non-exportable
 * Android Keystore AES-GCM key; SharedPreferences only receives IV+ciphertext.
 */
object BrainConnectionStore {
    private const val PREFS = "lingxi_brain_connection_v1"
    private const val KEY_ENDPOINT = "endpoint"
    private const val KEY_TOKEN_CIPHER = "token_cipher"
    private const val KEY_ALIAS = "lingxi_brain_bearer_v1"
    private const val TRANSFORMATION = "AES/GCM/NoPadding"

    @Volatile private var appContext: Context? = null

    fun initialize(context: Context) {
        appContext = context.applicationContext
    }

    private fun context(): Context = requireNotNull(appContext) {
        "BrainConnectionStore has not been initialized"
    }

    private fun prefs() = context().getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun configuredEndpoint(): String? = prefs().getString(KEY_ENDPOINT, null)?.takeIf { it.isNotBlank() }

    fun endpoint(): String? {
        configuredEndpoint()?.let { return it }
        return if (BuildConfig.DEBUG) BrainClient.DEFAULT_BASE_URL else null
    }

    fun hasToken(): Boolean = prefs().contains(KEY_TOKEN_CIPHER) && readToken() != null

    /** 仅供配对排障：短哈希能比对凭据版本，但不会还原或显示Bearer原文。 */
    fun tokenFingerprint(): String? = readToken()?.let { token ->
        MessageDigest.getInstance("SHA-256")
            .digest(token.toByteArray(Charsets.UTF_8))
            .take(6)
            .joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }

    @Synchronized
    fun save(endpoint: String, token: String?) {
        val checked = BrainConnectionPolicy.validateEndpoint(endpoint, BuildConfig.DEBUG)
        require(checked.valid) { checked.reason }
        val cleanToken = token?.trim()?.takeIf { it.isNotBlank() }
        if (cleanToken != null) {
            require(BrainConnectionPolicy.validToken(cleanToken)) { "设备令牌格式无效" }
        } else {
            require(hasToken()) { "首次配置必须填写设备令牌" }
        }
        val editor = prefs().edit().putString(KEY_ENDPOINT, checked.normalized)
        if (cleanToken != null) editor.putString(KEY_TOKEN_CIPHER, encrypt(cleanToken))
        check(editor.commit()) { "大脑配置保存失败" }
    }

    @Synchronized
    fun clear() {
        prefs().edit().clear().commit()
        runCatching {
            val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
            if (keyStore.containsAlias(KEY_ALIAS)) keyStore.deleteEntry(KEY_ALIAS)
        }
    }

    @Synchronized
    fun readToken(): String? {
        val packed = prefs().getString(KEY_TOKEN_CIPHER, null) ?: return null
        return runCatching {
            val parts = packed.split(':', limit = 2)
            require(parts.size == 2)
            val iv = Base64.decode(parts[0], Base64.NO_WRAP)
            val ciphertext = Base64.decode(parts[1], Base64.NO_WRAP)
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), GCMParameterSpec(128, iv))
            String(cipher.doFinal(ciphertext), Charsets.UTF_8)
                .takeIf(BrainConnectionPolicy::validToken)
        }.getOrNull()
    }

    private fun encrypt(token: String): String {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey())
        val iv = Base64.encodeToString(cipher.iv, Base64.NO_WRAP)
        val ciphertext = Base64.encodeToString(cipher.doFinal(token.toByteArray(Charsets.UTF_8)), Base64.NO_WRAP)
        return "$iv:$ciphertext"
    }

    private fun getOrCreateKey(): SecretKey {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        generator.init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true)
                .build()
        )
        return generator.generateKey()
    }
}

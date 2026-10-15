import java.io.File
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// 正式证书只从工作区外显源码的本机文件读取；文件存在但字段不全时直接失败，
// 防止误以为产出了正式签名包。没有该文件时仍允许构建 unsigned release 供静态审计。
val releaseSigningFile = System.getenv("LINGXI_RELEASE_SIGNING_FILE")
    ?.trim()
    ?.takeIf { it.isNotEmpty() }
    ?.let { rootProject.file(it) }
    ?: rootProject.file("../.secrets/release-signing.properties")
val releaseSigning = Properties().apply {
    if (releaseSigningFile.isFile) {
        releaseSigningFile.inputStream().use(::load)
    }
}
val releaseSigningKeys = listOf("storeFile", "storePassword", "keyAlias", "keyPassword")
if (releaseSigningFile.isFile) {
    val missing = releaseSigningKeys.filter { releaseSigning.getProperty(it).isNullOrBlank() }
    require(missing.isEmpty()) {
        "正式签名配置缺少字段: ${missing.joinToString()} (${releaseSigningFile.absolutePath})"
    }
}
val hasReleaseSigning = releaseSigningFile.isFile

android {
    namespace = "com.lingxi.mobile"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.lingxi.mobile"
        minSdk = 26          // Android 8.0：助手角色/前台服务等 API 的稳妥下限
        targetSdk = 34
        versionCode = 10000
        versionName = "1.0.0"

        // 只打包真机 arm64-v8a 的 sherpa-onnx native .so（演示主力 MI 8 Lite=arm64）。
        // 需跑 x86_64 模拟器时把 "x86_64" 加回来即可（jniLibs 里已含各 ABI 物料则一并打包）。
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    // ONNX 模型作为 assets 不压缩：sherpa-onnx 经 AAssetManager 直接读取，避免压缩流加载问题、也更快。
    androidResources {
        noCompress += listOf("onnx", "task")  // sherpa onnx + MediaPipe .task 模型按原样读取
    }

    signingConfigs {
        if (hasReleaseSigning) {
            create("lingxiRelease") {
                val configuredStore = File(releaseSigning.getProperty("storeFile"))
                storeFile = if (configuredStore.isAbsolute) configuredStore else {
                    releaseSigningFile.parentFile.resolve(configuredStore).canonicalFile
                }
                storePassword = releaseSigning.getProperty("storePassword")
                keyAlias = releaseSigning.getProperty("keyAlias")
                keyPassword = releaseSigning.getProperty("keyPassword")
                // minSdk=26（Android 8）已原生支持 V2；不再附带旧 JAR/V1 签名。
                enableV1Signing = false
                enableV2Signing = true
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("lingxiRelease")
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        viewBinding = true
        // Cloud-2 连接策略需要由构建变体明确区分 debug/release；
        // 不用可伪造的运行时输入来决定是否允许本机明文调试地址。
        buildConfig = true
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.4")
    implementation("androidx.recyclerview:recyclerview:1.3.2")

    // 网络：调大脑服务端 /interact
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("com.google.code.gson:gson:2.11.0")

    // 协程：网络异步
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")

    // 注视感知：MediaPipe FaceLandmarker(虹膜+blendshapes,本地·无 GMS·arm64) + CameraX 前置头
    implementation("com.google.mediapipe:tasks-vision:0.10.35")
    implementation("androidx.camera:camera-core:1.4.2")
    implementation("androidx.camera:camera-camera2:1.4.2")
    implementation("androidx.camera:camera-lifecycle:1.4.2")

    // 单元测试（JVM 跑，无需设备）：注视指代融合等纯逻辑回归
    testImplementation("junit:junit:4.13.2")
}

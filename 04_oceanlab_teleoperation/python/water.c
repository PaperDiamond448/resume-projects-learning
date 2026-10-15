#define _USE_MATH_DEFINES // 启用 M_PI 等数学常数
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm> // 添加 algorithm 头文件以支持 std::max_element
#include <cstdlib> // 用于system()调用MATLAB可执行文件

const char* vertexShaderSource = R"(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    void main() {
        gl_Position = projection * view * model * vec4(aPos, 1.0);
    }
)";

const char* fragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    uniform vec3 color;
    void main() {
        FragColor = vec4(color, 1.0);
    }
)";

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// 反射路径结构体
struct WavePath {
    std::vector<glm::vec3> points; // 路径上的点（起点、反射点、终点）
    float totalDistance; // 路径总长度
    float arrivalTime;  // 到达时间 (秒)
    float receivedLevel; // 接收声压级 (dB re 1μPa)
    glm::vec3 color; // 路径颜色
    float intensity; // 强度（用于衰减）
    GLuint VAO, VBO; // OpenGL 缓冲对象
    std::vector<float> vertices; // 顶点数据
};

// 发射换能器结构体
struct Emitter {
    float SL = 170.0f; // 声源级 (dB re 1μPa @ 1m)
    float directivityAngle = 30.0f; // 方向性主瓣宽度 (度)
    float DI = 0.0f; // 指向性指数 (dB)
};

// 接收换能器结构体
struct Receiver {
    float DT = 5.0f; // 检测阈值 (dB)
};

// 环境参数
struct Environment {
    float NL = 60.0f; // 噪声级 (dB re 1μPa)
    float alpha = 0.1f; // 介质吸收系数 (dB/m)
};

// 信号类型枚举
enum class SignalType {
    CW,          // 连续波
    CW_PULSE,    // CW脉冲
    LFM_PULSE    // 线性调频脉冲
};

// 信号参数结构体
struct SignalParameters {
    SignalType type = SignalType::CW; // 默认CW信号
    float frequency = 1000.0f;        // 信号频率 (Hz)
    float pulseDuration = 0.1f;       // 脉冲持续时间 (秒，仅脉冲信号)
    float lfmBandwidth = 500.0f;      // LFM带宽 (Hz，仅LFM脉冲)
};

// 生成波形和频谱数据
struct WaveformData {
    std::vector<float> waveform; // 时域波形
    std::vector<float> spectrum; // 频谱幅度
    std::vector<float> freqAxis; // 频率轴
};

WaveformData generateWaveform(const std::vector<WavePath>& wavePaths, float totalTime, int sampleCount, float noiseLevel, float maxSNR, const SignalParameters& signalParams) {
    WaveformData result;
    std::vector<float>& waveform = result.waveform;
    waveform.resize(sampleCount, 0.0f);
    float dt = totalTime / (sampleCount - 1); // 时间步长
    float fs = 1.0f / dt; // 采样频率

    // 计算最大信号幅度（用于归一化）
    float maxAmplitude = 0.0f;
    for (const auto& path : wavePaths) {
        float amplitude = pow(10.0f, path.receivedLevel / 20.0f);
        maxAmplitude = std::max(maxAmplitude, amplitude);
    }
    if (maxAmplitude == 0.0f) maxAmplitude = 1.0f; // 防止除零

    for (const auto& path : wavePaths) {
        float t0 = path.arrivalTime; // 到达时间
        float amplitude = pow(10.0f, path.receivedLevel / 20.0f) / maxAmplitude;

        for (int i = 0; i < sampleCount; ++i) {
            float t = i * dt;
            float signal = 0.0f;

            if (signalParams.type == SignalType::CW) {
                // 连续波：sin(2πft)
                signal = amplitude * sin(2.0f * M_PI * signalParams.frequency * (t - t0));
            }
            else if (signalParams.type == SignalType::CW_PULSE) {
                // CW脉冲：sin(2πft) * 矩形窗
                if (t >= t0 && t < t0 + signalParams.pulseDuration) {
                    signal = amplitude * sin(2.0f * M_PI * signalParams.frequency * (t - t0));
                }
            }
            else if (signalParams.type == SignalType::LFM_PULSE) {
                // LFM脉冲：sin(2π(f0t + 0.5μt^2)) * 矩形窗
                if (t >= t0 && t < t0 + signalParams.pulseDuration) {
                    float mu = signalParams.lfmBandwidth / signalParams.pulseDuration; // 调频率
                    float tau = t - t0;
                    float phase = 2.0f * M_PI * (signalParams.frequency * tau + 0.5f * mu * tau * tau);
                    signal = amplitude * sin(phase);
                }
            }

            waveform[i] += signal;
        }
    }

    // 添加噪声
    if (maxSNR < 100.0f) {
        float noiseAmplitude = pow(10.0f, (noiseLevel - maxSNR) / 20.0f) / maxAmplitude;
        for (int i = 0; i < sampleCount; ++i) {
            float noise = noiseAmplitude * (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            waveform[i] += noise;
        }
    }

    // 计算频谱（使用简单的DFT）
    int spectrumSize = sampleCount / 2;
    result.spectrum.resize(spectrumSize, 0.0f);
    result.freqAxis.resize(spectrumSize);
    float df = fs / sampleCount; // 频率分辨率

    for (int k = 0; k < spectrumSize; ++k) {
        float freq = k * df;
        result.freqAxis[k] = freq;
        float real = 0.0f, imag = 0.0f;
        for (int n = 0; n < sampleCount; ++n) {
            float angle = 2.0f * M_PI * k * n / sampleCount;
            real += waveform[n] * cos(angle);
            imag -= waveform[n] * sin(angle);
        }
        result.spectrum[k] = sqrt(real * real + imag * imag) / sampleCount;
    }

    // 归一化频谱
    float maxSpec = *std::max_element(result.spectrum.begin(), result.spectrum.end());
    if (maxSpec > 0.0f) {
        for (auto& val : result.spectrum) {
            val /= maxSpec;
        }
    }

    return result;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW 初始化失败" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 增大初始窗口尺寸为1280x960
    GLFWwindow* window = glfwCreateWindow(1280, 960, "Underwater Simulation", nullptr, nullptr);
    if (!window) {
        std::cerr << "窗口创建失败" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD 初始化失败" << std::endl;
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // 水池顶点数据 (3m x 2m x 1m)
    float poolVertices[] = {
        -1.5f, -1.0f,  0.0f,  1.5f, -1.0f,  0.0f,  1.5f,  1.0f,  0.0f,
         1.5f,  1.0f,  0.0f, -1.5f,  1.0f,  0.0f, -1.5f, -1.0f,  0.0f,
        -1.5f, -1.0f,  1.0f,  1.5f, -1.0f,  1.0f,  1.5f,  1.0f,  1.0f,
         1.5f,  1.0f,  1.0f, -1.5f,  1.0f,  1.0f, -1.5f, -1.0f,  1.0f
    };

    GLuint VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(poolVertices), poolVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 换能器顶点数据
    float transducerVertices[] = {
        -0.1f, -0.1f, -0.1f,  0.1f, -0.1f, -0.1f,  0.1f,  0.1f, -0.1f,
         0.1f,  0.1f, -0.1f, -0.1f,  0.1f, -0.1f, -0.1f, -0.1f, -0.1f,
        -0.1f, -0.1f,  0.1f,  0.1f, -0.1f,  0.1f,  0.1f,  0.1f,  0.1f,
         0.1f,  0.1f,  0.1f, -0.1f,  0.1f,  0.1f, -0.1f, -0.1f,  0.1f,
        -0.1f, -0.1f, -0.1f, -0.1f, -0.1f,  0.1f, -0.1f,  0.1f,  0.1f,
        -0.1f,  0.1f,  0.1f, -0.1f,  0.1f, -0.1f, -0.1f, -0.1f, -0.1f,
         0.1f, -0.1f, -0.1f,  0.1f, -0.1f,  0.1f,  0.1f,  0.1f,  0.1f,
         0.1f,  0.1f,  0.1f,  0.1f,  0.1f, -0.1f,  0.1f, -0.1f, -0.1f,
        -0.1f,  0.1f, -0.1f,  0.1f,  0.1f, -0.1f,  0.1f,  0.1f,  0.1f,
         0.1f,  0.1f,  0.1f, -0.1f,  0.1f,  0.1f, -0.1f,  0.1f, -0.1f,
        -0.1f, -0.1f, -0.1f,  0.1f, -0.1f, -0.1f,  0.1f, -0.1f,  0.1f,
         0.1f, -0.1f,  0.1f, -0.1f, -0.1f,  0.1f, -0.1f, -0.1f, -0.1f
    };

    GLuint emitterVAO, emitterVBO;
    glGenVertexArrays(1, &emitterVAO);
    glGenBuffers(1, &emitterVBO);
    glBindVertexArray(emitterVAO);
    glBindBuffer(GL_ARRAY_BUFFER, emitterVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(transducerVertices), transducerVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    GLuint receiverVAO, receiverVBO;
    glGenVertexArrays(1, &receiverVAO);
    glGenBuffers(1, &receiverVBO);
    glBindVertexArray(receiverVAO);
    glBindBuffer(GL_ARRAY_BUFFER, receiverVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(transducerVertices), transducerVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glEnable(GL_DEPTH_TEST);

    // 摄像机初始位置
    glm::vec3 cameraPos = glm::vec3(5.0f, 5.0f, 5.0f);
    float cameraSpeed = 2.0f; // 摄像机移动速度 (m/s)

    // 声波参数
    static bool emitWave = false;
    static float waveTime = 0.0f;
    static float waveSpeed = 1.5f; // 声速 1.5 m/s
    const float decayFactor = 0.8f; // 每次反射的衰减系数
    const int maxReflections = 3; // 最大反射次数

    // 换能器和环境参数
    Emitter emitter;
    Receiver receiver;
    Environment env;
    SignalParameters signalParams;

    // 测试参数
    float TL = 0.0f; // 传播损失 (dB)
    float RL = 0.0f; // 接收声压级 (dB re 1μPa)
    float SNR = 0.0f; // 信噪比 (dB)
    bool detectable = false; // 是否可检测

    // 存储所有反射路径
    std::vector<WavePath> wavePaths;

    // 波形数据
    static WaveformData waveformData;
    static std::vector<float> timeAxis;
    static bool waveformGenerated = false;

    // 计算反射路径的函数
    auto calculateReflectionPath = [&](glm::vec3 start, glm::vec3 end, const std::vector<int>& reflectionOrder) {
        WavePath path;
        path.points.push_back(start);
        path.intensity = 1.0f;
        path.color = glm::vec3(0.0f, 1.0f, 1.0f); // 初始颜色（青色）

        glm::vec3 currentPos = start;
        for (int i = 0; i < reflectionOrder.size(); ++i) {
            int boundary = reflectionOrder[i];
            glm::vec3 mirrorEnd;

            if (boundary == 0) { // 水面 (z = 1.0)
                mirrorEnd = glm::vec3(end.x, end.y, 2.0f - end.z);
            }
            else if (boundary == 1) { // 池底 (z = 0.0)
                mirrorEnd = glm::vec3(end.x, end.y, -end.z);
            }
            else if (boundary == 2) { // 左壁 (x = -1.5)
                mirrorEnd = glm::vec3(-3.0f - end.x, end.y, end.z);
            }
            else if (boundary == 3) { // 右壁 (x = 1.5)
                mirrorEnd = glm::vec3(3.0f - end.x, end.y, end.z);
            }
            else if (boundary == 4) { // 前壁 (y = -1.0)
                mirrorEnd = glm::vec3(end.x, -2.0f - end.y, end.z);
            }
            else { // 后壁 (y = 1.0)
                mirrorEnd = glm::vec3(end.x, 2.0f - end.y, end.z);
            }

            glm::vec3 dir = glm::normalize(mirrorEnd - currentPos);
            float t;
            if (boundary == 0) t = (1.0f - currentPos.z) / dir.z;
            else if (boundary == 1) t = (0.0f - currentPos.z) / dir.z;
            else if (boundary == 2) t = (-1.5f - currentPos.x) / dir.x;
            else if (boundary == 3) t = (1.5f - currentPos.x) / dir.x;
            else if (boundary == 4) t = (-1.0f - currentPos.y) / dir.y;
            else t = (1.0f - currentPos.y) / dir.y;

            glm::vec3 reflectionPoint = currentPos + t * dir;
            path.points.push_back(reflectionPoint);
            path.intensity *= decayFactor;
            currentPos = reflectionPoint;
            end = (i == reflectionOrder.size() - 1) ? end : reflectionPoint;
        }
        path.points.push_back(end);

        path.totalDistance = 0.0f;
        for (size_t i = 0; i < path.points.size() - 1; ++i) {
            path.totalDistance += glm::length(path.points[i + 1] - path.points[i]);
        }

        // 计算到达时间：距离 / 声速
        path.arrivalTime = path.totalDistance / waveSpeed;

        // 计算传播损失 (TL)
        float distance = path.totalDistance;
        float TL = 20.0f * log10(distance) + env.alpha * distance;

        // 计算接收声压级 (RL)
        path.receivedLevel = emitter.SL - TL + emitter.DI;

        // 考虑反射衰减对 RL 的影响
        path.receivedLevel += 20.0f * log10(path.intensity); // 每次反射的强度衰减转换为分贝

        path.color *= path.intensity;

        path.vertices.resize((path.points.size() - 1) * 6);
        for (size_t i = 0; i < path.points.size() - 1; ++i) {
            path.vertices[i * 6 + 0] = path.points[i].x;
            path.vertices[i * 6 + 1] = path.points[i].y;
            path.vertices[i * 6 + 2] = path.points[i].z + 0.01f;
            path.vertices[i * 6 + 3] = path.points[i].x;
            path.vertices[i * 6 + 4] = path.points[i].y;
            path.vertices[i * 6 + 5] = path.points[i].z + 0.01f;
        }

        glGenVertexArrays(1, &path.VAO);
        glGenBuffers(1, &path.VBO);
        glBindVertexArray(path.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, path.VBO);
        glBufferData(GL_ARRAY_BUFFER, path.vertices.size() * sizeof(float), path.vertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        return path;
    };

    // 记录上一次的时间
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        // 计算时间差
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        // 摄像机移动
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            cameraPos += cameraSpeed * deltaTime * glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f));
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            cameraPos -= cameraSpeed * deltaTime * glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f));
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            cameraPos -= cameraSpeed * deltaTime * glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f));
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            cameraPos += cameraSpeed * deltaTime * glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f));
        }

        // 计算指向性指数
        emitter.DI = 10.0f * log10(41253.0f / (emitter.directivityAngle * emitter.directivityAngle));

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.1f, 0.2f, 0.3f, 1.0f);

        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        // 动态计算投影矩阵，根据窗口尺寸调整宽高比
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // 绘制水池
        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(glGetUniformLocation(shaderProgram, "color"), 0.5f, 0.5f, 0.5f);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glUniform3f(glGetUniformLocation(shaderProgram, "color"), 0.0f, 0.5f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 6, 6);

        // 绘制发射换能器
        static float emitterPos[3] = { 0.0f, 0.0f, 0.5f };
        glm::mat4 emitterModel = glm::translate(glm::mat4(1.0f), glm::vec3(emitterPos[0], emitterPos[1], emitterPos[2]));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(emitterModel));
        glUniform3f(glGetUniformLocation(shaderProgram, "color"), 1.0f, 0.0f, 0.0f);
        glBindVertexArray(emitterVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // 绘制接收换能器
        static float receiverPos[3] = { 1.0f, 0.0f, 0.5f };
        glm::mat4 receiverModel = glm::translate(glm::mat4(1.0f), glm::vec3(receiverPos[0], receiverPos[1], receiverPos[2]));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(receiverModel));
        glUniform3f(glGetUniformLocation(shaderProgram, "color"), 0.0f, 1.0f, 0.0f);
        glBindVertexArray(receiverVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // 声波传播
        if (emitWave) {
            waveTime += deltaTime;

            // 绘制所有路径
            for (auto& path : wavePaths) {
                float waveDistance = waveSpeed * waveTime;
                if (waveDistance >= path.totalDistance) {
                    // 路径传播完成，计算测试参数（仅对直达波）
                    if (path.points.size() == 2) { // 直达波
                        // 计算传播损失
                        float distance = path.totalDistance;
                        TL = 20.0f * log10(distance) + env.alpha * distance;

                        // 计算接收声压级
                        RL = emitter.SL - TL + emitter.DI;

                        // 计算信噪比
                        SNR = RL - env.NL;

                        // 判断是否可检测
                        detectable = (SNR > receiver.DT);
                    }
                    continue;
                }

                size_t numSegments = path.points.size() - 1;
                float accumulatedDistance = 0.0f;
                size_t currentSegment = 0;

                for (size_t i = 0; i < numSegments; ++i) {
                    float segmentLength = glm::length(path.points[i + 1] - path.points[i]);
                    if (waveDistance < accumulatedDistance + segmentLength) {
                        currentSegment = i;
                        break;
                    }
                    accumulatedDistance += segmentLength;
                }

                for (size_t i = 0; i < numSegments; ++i) {
                    if (i < currentSegment) {
                        path.vertices[i * 6 + 0] = path.points[i].x;
                        path.vertices[i * 6 + 1] = path.points[i].y;
                        path.vertices[i * 6 + 2] = path.points[i].z + 0.01f;
                        path.vertices[i * 6 + 3] = path.points[i + 1].x;
                        path.vertices[i * 6 + 4] = path.points[i + 1].y;
                        path.vertices[i * 6 + 5] = path.points[i + 1].z + 0.01f;
                    }
                    else if (i == currentSegment) {
                        float remainingDistance = waveDistance - accumulatedDistance;
                        glm::vec3 dir = glm::normalize(path.points[i + 1] - path.points[i]);
                        glm::vec3 waveEnd = path.points[i] + dir * remainingDistance;
                        path.vertices[i * 6 + 0] = path.points[i].x;
                        path.vertices[i * 6 + 1] = path.points[i].y;
                        path.vertices[i * 6 + 2] = path.points[i].z + 0.01f;
                        path.vertices[i * 6 + 3] = waveEnd.x;
                        path.vertices[i * 6 + 4] = waveEnd.y;
                        path.vertices[i * 6 + 5] = waveEnd.z + 0.01f;
                    }
                    else {
                        path.vertices[i * 6 + 0] = path.points[i].x;
                        path.vertices[i * 6 + 1] = path.points[i].y;
                        path.vertices[i * 6 + 2] = path.points[i].z + 0.01f;
                        path.vertices[i * 6 + 3] = path.points[i].x;
                        path.vertices[i * 6 + 4] = path.points[i].y;
                        path.vertices[i * 6 + 5] = path.points[i].z + 0.01f;
                    }
                }

                glBindBuffer(GL_ARRAY_BUFFER, path.VBO);
                glBufferData(GL_ARRAY_BUFFER, path.vertices.size() * sizeof(float), path.vertices.data(), GL_DYNAMIC_DRAW);

                glm::mat4 waveModel = glm::mat4(1.0f);
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(waveModel));
                glUniform3f(glGetUniformLocation(shaderProgram, "color"), path.color.x, path.color.y, path.color.z);
                glBindVertexArray(path.VAO);
                glLineWidth(3.0f);
                glDrawArrays(GL_LINES, 0, path.vertices.size() / 3);
                glLineWidth(1.0f);
            }

            bool allDone = true;
            for (const auto& path : wavePaths) {
                if (waveSpeed * waveTime < path.totalDistance) {
                    allDone = false;
                    break;
                }
            }
            if (allDone) {
                emitWave = false;
                std::cout << "All waves reached receiver!" << std::endl;
            }
        }

        // ImGui 控制
        ImGui::Begin("Control Panel");
        ImGui::Text("Underwater Simulation");
        ImGui::SliderFloat3("Emitter Position", emitterPos, -1.5f, 1.5f);
        ImGui::SliderFloat3("Receiver Position", receiverPos, -1.5f, 1.5f);

        // 发射换能器参数
        ImGui::Text("Emitter Parameters:");
        ImGui::SliderFloat("Source Level (SL, dB)", &emitter.SL, 150.0f, 200.0f);
        ImGui::SliderFloat("Directivity Angle (deg)", &emitter.directivityAngle, 10.0f, 90.0f);
        ImGui::Text("Directivity Index (DI): %.2f dB", emitter.DI);

        // 信号参数
        ImGui::Text("Signal Parameters:");
        const char* signalTypes[] = { "CW", "CW Pulse", "LFM Pulse" };
        static int currentSignalType = 0;
        if (ImGui::Combo("Signal Type", &currentSignalType, signalTypes, IM_ARRAYSIZE(signalTypes))) {
            signalParams.type = static_cast<SignalType>(currentSignalType);
        }
        ImGui::SliderFloat("Frequency (Hz)", &signalParams.frequency, 100.0f, 5000.0f);
        if (signalParams.type != SignalType::CW) {
            ImGui::SliderFloat("Pulse Duration (s)", &signalParams.pulseDuration, 0.01f, 0.5f);
        }
        if (signalParams.type == SignalType::LFM_PULSE) {
            ImGui::SliderFloat("LFM Bandwidth (Hz)", &signalParams.lfmBandwidth, 100.0f, 2000.0f);
        }

        // 接收换能器参数
        ImGui::Text("Receiver Parameters:");
        ImGui::SliderFloat("Detection Threshold (DT, dB)", &receiver.DT, 0.0f, 150.0f);

        // 环境参数
        ImGui::Text("Environment Parameters:");
        ImGui::SliderFloat("Noise Level (NL, dB)", &env.NL, 40.0f, 120.0f);
        ImGui::SliderFloat("Absorption Coefficient (alpha, dB/m)", &env.alpha, 0.0f, 1.0f);

        if (ImGui::Button("Emit Wave")) {
            emitWave = true;
            waveTime = 0.0f;
            TL = 0.0f;
            RL = 0.0f;
            SNR = 0.0f;
            detectable = false;

            // 清空之前的路径
            for (auto& path : wavePaths) {
                glDeleteVertexArrays(1, &path.VAO);
                glDeleteBuffers(1, &path.VBO);
            }
            wavePaths.clear();

            // 计算所有可能的反射路径
            glm::vec3 start = glm::vec3(emitterPos[0], emitterPos[1], emitterPos[2]);
            glm::vec3 end = glm::vec3(receiverPos[0], receiverPos[1], receiverPos[2]);

            // 直达波
            wavePaths.push_back(calculateReflectionPath(start, end, {}));

            // 1 次反射
            wavePaths.push_back(calculateReflectionPath(start, end, { 0 }));
            wavePaths.push_back(calculateReflectionPath(start, end, { 1 }));
            wavePaths.push_back(calculateReflectionPath(start, end, { 2 }));
            wavePaths.push_back(calculateReflectionPath(start, end, { 3 }));
            wavePaths.push_back(calculateReflectionPath(start, end, { 4 }));
            wavePaths.push_back(calculateReflectionPath(start, end, { 5 }));

            // 2 次反射
            wavePaths.push_back(calculateReflectionPath(start, end, { 0, 1 }));
            wavePaths.push_back(calculateReflectionPath(start, end, { 0, 2 }));

            // 3 次反射
            wavePaths.push_back(calculateReflectionPath(start, end, { 0, 1, 2 }));

            // 生成波形数据
            float maxTime = 0.0f;
            for (const auto& path : wavePaths) {
                maxTime = std::max(maxTime, path.arrivalTime);
            }
            maxTime += 0.5f;
            const int sampleCount = 1000;

            float maxSNR = 0.0f;
            for (const auto& path : wavePaths) {
                if (path.points.size() == 2) {
                    maxSNR = path.receivedLevel - env.NL;
                    break;
                }
            }

            waveformData = generateWaveform(wavePaths, maxTime, sampleCount, env.NL, maxSNR, signalParams);

            // 生成时间轴
            timeAxis.resize(sampleCount);
            float dt = maxTime / (sampleCount - 1);
            for (int i = 0; i < sampleCount; ++i) {
                timeAxis[i] = i * dt;
            }

            waveformGenerated = true;

            std::cout << "Emit Wave button clicked! emitWave = " << (emitWave ? "true" : "false") << std::endl;
        }

        // 新增按钮：调用MATLAB可执行文件
        if (ImGui::Button("Run MATLAB Simulation")) {
            // 替换为你的MATLAB可执行文件路径
            std::string matlabExePath = "D:/opengl/Test4/ConsoleApplication1/hydrophone_simulation_copy "; // 如果路径有空格，使用双引号，例如 "\"C:/Program Files/hydrophone_simulation.exe\""
            int result = system(matlabExePath.c_str());
            if (result == 0) {
                std::cout << "MATLAB可执行文件成功运行！" << std::endl;
            }
            else {
                std::cout << "MATLAB可执行文件运行失败，返回码：" << result << std::endl;
            }
        }

        // 显示测试结果
        ImGui::Text("Test Results:");
        ImGui::Text("Transmission Loss (TL): %.2f dB", TL);
        ImGui::Text("Received Level (RL): %.2f dB re 1uPa", RL);
        ImGui::Text("Signal-to-Noise Ratio (SNR): %.2f dB", SNR);
        ImGui::Text("Detectable: %s", detectable ? "Yes" : "No");

        // 绘制波形图和频谱图
        if (waveformGenerated) {
            if (ImPlot::BeginPlot("Echo Waveform")) {
                ImPlot::SetupAxes("Time (s)", "Amplitude");
                ImPlot::PlotLine("Waveform", timeAxis.data(), waveformData.waveform.data(), waveformData.waveform.size());
                ImPlot::EndPlot();
            }
            if (ImPlot::BeginPlot("Spectrum")) {
                ImPlot::SetupAxes("Frequency (Hz)", "Magnitude");
                ImPlot::PlotLine("Spectrum", waveformData.freqAxis.data(), waveformData.spectrum.data(), waveformData.spectrum.size());
                ImPlot::EndPlot();
            }
        }

        ImGui::Text("Camera Position: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // 清理资源
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &emitterVAO);
    glDeleteBuffers(1, &emitterVBO);
    glDeleteVertexArrays(1, &receiverVAO);
    glDeleteBuffers(1, &receiverVBO);
    for (auto& path : wavePaths) {
        glDeleteVertexArrays(1, &path.VAO);
        glDeleteBuffers(1, &path.VBO);
    }
    glDeleteProgram(shaderProgram);
    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}
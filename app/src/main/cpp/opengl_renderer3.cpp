//
// Created by zhangx on 2026/1/3.
// OpenGL Renderer 3 - 使用共享工具库的简化版本
//

#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include "opengl_utils.h"
#include <sys/time.h>

#define LOG_TAG "OpenGLRenderer3"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 渲染器状态
static struct {
    GLuint program;
    GLuint textureID;
    GLuint g_tfb[2];  // 双缓冲：ping-pong buffers
    int currentBuffer; // 当前读取的缓冲区索引 (0 或 1)
    MeshData mesh;
    int particle_count;
    bool initialized;
} gRenderer = {0};

static struct {
    UniformBuffer ubo;
    float cameraPos[3];
    float aspectRatio;
} g_Camera_Uniforms;

static struct {
    UniformBuffer ubo;
    float spoutPos[3];
    float gravity[3];
    float deltaTime;
    float maxLifeTime;
    float currentTime;  // 累积时间
} g_Particle_Uniforms;

static const int BINDING_POINT_TFB =0;
static const int BINDING_POINT_VAO =1;
void updateParticlesWithTFB();
void renderParticles();

const GLchar* g_TransformFeedbackVaryings[] = {
        "vPosition",
        "vDiameter",
        "vVelocity",
        "vLifetime"
};


// 顶点着色器（简化版，可根据需要修改）
static const char* vertexShaderSource = R"(#version 300 es

layout (location = 0) in vec3 aPosition;
layout (location = 1) in float diameter;
layout (location = 2) in vec3 aVelocity;
layout (location = 3) in float aLifetime;

layout(std140) uniform CameraUniforms {
        float uAspectRatio;    // 宽高比
        vec3 uCameraPos;   // 相机位置
    };
layout(std140) uniform ParticleUniforms {
        float uDeltaTime; //帧时间增量
        vec3 uSpoutPos;   // 喷口位置
        vec3 uGravity;  // 重力加速度向量 (0, -9.8, 0) 或类似值
        float uMaxLifeTime;  // 最大生命周期
        float uCurrentTime;  // 累积时间（用于随机扰动）
    };

// 改进的哈希函数：打破线性相关性
highp float hash(highp float n) {
    return fract(sin(n * 127.1) * 43758.5453123);
}

// 2D 哈希函数：确保不同输入产生独立的随机数
highp float hash2D(vec2 p) {
    // 使用多个质数混淆
    p = fract(p * vec2(443.8975, 397.2973));
    p += dot(p.yx, p.xy + vec2(21.5351, 14.3137));
    return fract(p.x * p.y * 95.4307);
}

// 随机函数（GPU生成随机数）
highp float random(vec2 co) {
    return hash2D(co);
}

// 输出：更新后的粒子属性（供TFB捕获）
out vec3 vPosition;
out float vDiameter;
out vec3 vVelocity;
out float vLifetime;
out float vAlpha;

void main() {
    vec3 currentPos = aPosition;
    float currentDiameter = diameter;
    vec3 currentVel = aVelocity;
    float currentLife = aLifetime - uDeltaTime;

    // 使用 gl_VertexID 作为每个粒子的唯一标识（用于随机种子）
    float particleID = float(gl_VertexID);

    if (currentLife <= 0.0f) {
        //生命周期结束，重置粒子
        currentPos = uSpoutPos;
        
        // 使用模运算将累积时间限制在 0-100 范围内，避免精度问题
        // 乘以不同的质数让每个属性的扰动独立
        float timeMod = mod(uCurrentTime, 100.0);
        float timePert1 = mod(timeMod * 17.3, 10.0);   // 0-10 范围
        float timePert2 = mod(timeMod * 23.7, 10.0);
        float timePert3 = mod(timeMod * 31.1, 10.0);
        float timePert4 = mod(timeMod * 41.9, 10.0);
        float timePert5 = mod(timeMod * 53.3, 10.0);
        
        // 为每个属性使用完全独立的种子
        // particleID 为主（0-200），时间扰动为辅（0-10），影响比例约 5%
        float seed1 = hash(particleID * 0.1234 + timePert1 * 0.01);           // 直径种子
        float seed2 = hash(particleID * 0.5678 + 100.0 + timePert2 * 0.01);   // X速度种子
        float seed3 = hash(particleID * 0.9012 + 200.0 + timePert3 * 0.01);   // Y速度种子
        float seed4 = hash(particleID * 0.3456 + 300.0 + timePert4 * 0.01);   // Z速度种子
        float seed5 = hash(particleID * 0.7890 + 400.0 + timePert5 * 0.01);   // 生命周期种子
        
        currentDiameter = seed1 * 0.5f + 0.5f;  // 直径 0.5-1.0
        
        // 给粒子一个向上的初始速度，每个方向完全独立
        currentVel = vec3(
            (seed2 - 0.5f) * 0.3f,      // X方向随机速度
            seed3 * 0.8f + 0.5f,        // Y方向向上速度 0.5-1.3
            (seed4 - 0.5f) * 0.3f       // Z方向随机速度
        );
        currentLife = seed5 * 2.0f + 3.0f;  // 生命周期 3-5秒
    } else {
        // 应用重力（抛物线运动）
        currentVel = currentVel + uGravity * uDeltaTime;
        // 更新位置
        currentPos = currentPos + currentVel * uDeltaTime;
    }
    vAlpha = clamp(currentLife / uMaxLifeTime, 0.0f, 1.0f); // 透明度，随着生命周期衰减
    vPosition = currentPos;
    vDiameter = currentDiameter;
    vVelocity = currentVel;
    vLifetime = currentLife;


    // 设置顶点位置（用于渲染）
    gl_Position = vec4(currentPos.x / uAspectRatio, currentPos.y, currentPos.z, 1.0);
    gl_PointSize = vDiameter * 50.0;  // 放大粒子，使其可见
}
)";

// 片段着色器
static const char* fragmentShaderSource = R"(#version 300 es
precision mediump float;

out vec4 fragColor;
in float vAlpha;
in float vDiameter;  // 接收直径值
void main() {
    //圆形粒子 gl_PointCoord 的范围是 [0, 1] 所以要控制vDiameter在[0, 2]之间
    float radius = vDiameter / 2.0f;
    float dist = distance(gl_PointCoord, vec2(0.5, 0.5));
    if (dist > radius) {
        discard;
    }
    //白色，透明度随生命周期衰减
    fragColor = vec4(1.0, 1.0, 1.0, vAlpha);
}
)";

static float last_time = 0.0f;

// 初始化渲染器
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_nativeInit(JNIEnv* env, jobject thiz) {
    LOGI("Initializing Renderer3");
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    // 检查 OpenGL 上下文
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("OpenGL error before shader creation: 0x%x", err);
    }
    
    // 编译着色器程序
    gRenderer.particle_count = 200;
    gRenderer.program = createProgram(vertexShaderSource, fragmentShaderSource);
    
    if (gRenderer.program == 0) {
        LOGE("Failed to create shader program - check shader compilation errors above");
        gRenderer.initialized = false;
        return JNI_FALSE;
    }
    
    // 开启点精灵（渲染粒子为点）
    glEnable(GL_PROGRAM_POINT_SIZE);
    gRenderer.initialized = true;
    LOGI("Renderer3 initialized successfully, program=%d", gRenderer.program);

    //初始化统一变量ubo
    g_Camera_Uniforms.ubo = createUniformBuffer(gRenderer.program, "CameraUniforms", 0);
//    updateUniformBuffer(&g_Camera_Uniforms.ubo, &g_Camera_Uniforms.aspectRatio, 0, sizeof(g_Camera_Uniforms.aspectRatio));


    g_Particle_Uniforms.ubo = createUniformBuffer(gRenderer.program, "ParticleUniforms", 1);
    g_Particle_Uniforms.deltaTime = 0.0f;
    g_Particle_Uniforms.currentTime = 0.0f;
    float spoutPosTemp[] = {0.0f, -0.8f, 0.0f};  // 屏幕下方
    float gravityTemp[] = {0.0f, -0.5f, 0.0f};   // 降低重力，粒子飞得更高更慢
    memcpy(g_Particle_Uniforms.spoutPos, spoutPosTemp, sizeof(spoutPosTemp));
    memcpy(g_Particle_Uniforms.gravity, gravityTemp, sizeof(gravityTemp));
    g_Particle_Uniforms.maxLifeTime = MAX_LIFE_TIME;
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.deltaTime, 0, sizeof(g_Particle_Uniforms.deltaTime));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.spoutPos, 16, sizeof(g_Particle_Uniforms.spoutPos));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.gravity, 32, sizeof(g_Particle_Uniforms.gravity));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.maxLifeTime, 44, sizeof(g_Particle_Uniforms.maxLifeTime));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.currentTime, 48, sizeof(g_Particle_Uniforms.currentTime));
    return JNI_TRUE;
}

// 从 Bitmap 加载纹理
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_loadTextureFromBitmap(JNIEnv *env, jobject thiz, jobject bitmap) {
    if (gRenderer.textureID != 0) {
        releaseTexture(gRenderer.textureID);
    }
    gRenderer.textureID = loadTextureFromBitmap(env, bitmap);
    if (gRenderer.textureID == 0) {
        LOGE("Failed to load texture from bitmap");
    }
}

// 释放纹理
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_releaseTexture(JNIEnv *env, jobject thiz) {
    releaseTexture(gRenderer.textureID);
    gRenderer.textureID = 0;
}

// 改变视口大小
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_nativeResize(JNIEnv *env, jobject thiz, jint width, jint height) {
    LOGI("Resizing viewport to %d x %d", width, height);
    glViewport(0, 0, width, height);
    g_Camera_Uniforms.aspectRatio = (float)width / (float)height;
    updateUniformBuffer(&g_Camera_Uniforms.ubo, &g_Camera_Uniforms.aspectRatio, 0, sizeof(g_Camera_Uniforms.aspectRatio));
}

static int frameCount = 0;

// 渲染一帧
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_nativeRender(JNIEnv *env, jobject thiz) {
    if (!gRenderer.initialized || gRenderer.program == 0) {
        LOGE("Renderer not initialized");
        return;
    }
    
    if (gRenderer.mesh.vao == 0 || gRenderer.g_tfb[0] == 0 || gRenderer.g_tfb[1] == 0) {
        LOGE("VAO or TFB not initialized: vao=%d, tfb[0]=%d, tfb[1]=%d", 
             gRenderer.mesh.vao, gRenderer.g_tfb[0], gRenderer.g_tfb[1]);
        return;
    }
    
    frameCount++;
    if (frameCount % 60 == 0) {  // 每60帧打印一次
        LOGI("Rendering frame %d", frameCount);
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(gRenderer.program);

    //设置统一变量
    //当前时间（使用毫秒精度）
    struct timeval tv;
    gettimeofday(&tv, NULL);
    float currentTime = (float)tv.tv_sec + (float)tv.tv_usec / 1000000.0f;
    float deltaTime;
    
    if (last_time == 0.0f){
        // 第一帧使用一个小的固定值，确保粒子开始移动
        deltaTime = 0.016f;  // 约 60 FPS 的帧时间
        last_time = currentTime;
//        LOGI("First frame: deltaTime=%.6f, currentTime=%.6f", deltaTime, currentTime);
    } else {
        deltaTime = currentTime - last_time;
        // 限制 deltaTime 在合理范围内（避免时间跳跃或负值）
        if (deltaTime <= 0.0f || deltaTime > 0.1f) {
//            LOGI("DeltaTime out of range: %.6f, clamping to 0.016f", deltaTime);
            deltaTime = 0.016f;  // 如果时间异常，使用固定值
        }
        last_time = currentTime;
    }
    
    // 确保 deltaTime 不为 0
    if (deltaTime <= 0.0f) {
//        LOGE("ERROR: deltaTime is zero or negative: %.6f", deltaTime);
        deltaTime = 0.016f;  // 强制设置为一个非零值
    }
    
    g_Particle_Uniforms.deltaTime = deltaTime;
    g_Particle_Uniforms.currentTime = currentTime;  // 更新累积时间
    
    // 确保 UBO 已绑定
    if (g_Particle_Uniforms.ubo.ubo == 0) {
        LOGE("Particle UBO is not initialized!");
    } else {
        updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.deltaTime, 0, sizeof(g_Particle_Uniforms.deltaTime));
        // 注意：UBO 中 uCurrentTime 在 offset 48 之后（uMaxLifeTime 在 44-47）
        updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.currentTime, 48, sizeof(g_Particle_Uniforms.currentTime));
    }
    
//    if (frameCount <= 10 || frameCount % 60 == 0) {
//        LOGI("Frame %d: deltaTime=%.6f, currentTime=%.6f, last_time=%.6f, particle_count=%d, currentBuffer=%d",
//             frameCount, deltaTime, currentTime, last_time, gRenderer.particle_count, gRenderer.currentBuffer);
//    }


    // 绑定纹理
    if (gRenderer.textureID != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gRenderer.textureID);
        GLint textureLoc = glGetUniformLocation(gRenderer.program, "uTexture");
        if (textureLoc != -1) {
            glUniform1i(textureLoc, 0);
        }
    }


    // 绑定并绘制网格
    if (gRenderer.mesh.vao != 0) {
        glBindVertexArray(gRenderer.mesh.vao);
        if (gRenderer.mesh.indexCount > 0) {
            glDrawElements(GL_TRIANGLES, gRenderer.mesh.indexCount, GL_UNSIGNED_INT, 0);
        } else if (gRenderer.g_tfb[0] != 0 && gRenderer.g_tfb[1] != 0){
            //两次 glDrawArrays 看似重复，实则目的完全不同：
            //第一次是 "更新粒子数据"（只跑顶点着色器，不渲染），
            //第二次是 "渲染粒子"（跑完整管线，显示到屏幕）。
            if (frameCount == 1) {
                LOGI("Drawing particles for first time, currentBuffer=%d", gRenderer.currentBuffer);
            }
            // 更新粒子（使用 Transform Feedback）
            updateParticlesWithTFB();
            // 渲染更新后的粒子
            renderParticles();
            
            // 检查 OpenGL 错误
            GLenum err = glGetError();
            if (err != GL_NO_ERROR && frameCount <= 5) {
                LOGE("OpenGL error after drawing: 0x%x", err);
            }
        }
        glBindVertexArray(0);
    }

    glUseProgram(0);
}

// 清理资源
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_nativeCleanup(JNIEnv *env, jobject thiz) {
    releaseMesh(&gRenderer.mesh);
    releaseTexture(gRenderer.textureID);

    // 释放双缓冲 TFB
    if (gRenderer.g_tfb[0] != 0) {
        glDeleteBuffers(1, &gRenderer.g_tfb[0]);
        gRenderer.g_tfb[0] = 0;
    }
    if (gRenderer.g_tfb[1] != 0) {
        glDeleteBuffers(1, &gRenderer.g_tfb[1]);
        gRenderer.g_tfb[1] = 0;
    }

    if (gRenderer.program != 0) {
        glDeleteProgram(gRenderer.program);
        gRenderer.program = 0;
    }

    gRenderer.initialized = false;
    LOGI("Renderer3 resources cleaned up");
}


extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_initTFBBuffer(JNIEnv *env, jobject thiz) {
    if (gRenderer.program == 0) {
        LOGE("Cannot initialize TFB buffer: program is not created");
        return;
    }
    
    LOGI("Initializing TFB buffers (double buffered) with %d particles", gRenderer.particle_count);
    
    // 创建双缓冲
    glGenBuffers(2, gRenderer.g_tfb);
    gRenderer.currentBuffer = 0;  // 初始从缓冲区0读取
    
    //分配大小
    int buffer_size = gRenderer.particle_count * sizeof(Particle);

    // 创建并初始化粒子数据
    Particle* particles = new Particle[gRenderer.particle_count];
    for (int i = 0; i < gRenderer.particle_count; i++) {
        // 给每个粒子一个唯一的初始位置（作为随机种子）
        // 使用索引来生成不同的初始值
        float seed = (float)i;
        particles[i].position[0] = (seed * 0.01f) - 0.5f;  // 稍微分散，避免完全重叠
        particles[i].position[1] = -0.8f;  // 喷口位置
        particles[i].position[2] = (seed * 0.01f) - 0.5f;
        
        // 初始直径（会在重置时随机）
        particles[i].diameter = 1.0f;
        
        // 初始速度为0
        particles[i].velocity[0] = 0.0f;
        particles[i].velocity[1] = 0.0f;
        particles[i].velocity[2] = 0.0f;
        
        // 生命周期设为负数或0，触发第一帧重置
        // 为了让粒子不同时重置，可以设置不同的初始生命周期
        particles[i].lifeTime = -((float)i / (float)gRenderer.particle_count) * 3.0f;
    }

    // 初始化两个缓冲区（内容相同）
    for (int i = 0; i < 2; i++) {
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, gRenderer.g_tfb[i]);
        glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, buffer_size, particles, GL_DYNAMIC_COPY);
    }
    delete[] particles;  // 释放临时数组
    
    //指定TFB要捕获的变量
    glTransformFeedbackVaryings(gRenderer.program, 4, g_TransformFeedbackVaryings, GL_INTERLEAVED_ATTRIBS);

    // 重新链接着色器程序（使TFB变量设置生效）
    glLinkProgram(gRenderer.program);

    //检查链接状态
    GLint status;
    glGetProgramiv(gRenderer.program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        //抛出错误
        GLint infoLen = 0;
        glGetProgramiv(gRenderer.program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char* infoLog = new char[infoLen];
            glGetProgramInfoLog(gRenderer.program, infoLen, nullptr, infoLog);
            LOGE("Program link failed after TFB setup: %s", infoLog);
            delete[] infoLog;
        } else {
            LOGE("Program link failed after TFB setup (no error log)");
        }
        // 解绑并返回，不继续初始化
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
        gRenderer.initialized = false;
        return;
    } else {
        LOGI("TFB buffer initialized successfully, program relinked");
    }
    //解绑
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);

}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_initVAO(JNIEnv *env, jobject thiz) {
    if (gRenderer.program == 0) {
        LOGE("Cannot initialize VAO: program is not created");
        return;
    }
    if (gRenderer.g_tfb[0] == 0 || gRenderer.g_tfb[1] == 0) {
        LOGE("Cannot initialize VAO: TFB buffers are not created");
        return;
    }
    
    LOGI("Initializing VAO");
    
    glGenVertexArrays(1, &gRenderer.mesh.vao);
    glBindVertexArray(gRenderer.mesh.vao);
    //绑定TFB缓冲区作为顶点缓冲区（因为粒子数据存在这里）
    // 初始绑定到缓冲区0
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.g_tfb[0]);

    //绑定顶点属性（对应顶点着色器的in变量）
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, diameter)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, velocity)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, lifeTime)));
    glEnableVertexAttribArray(3);
    //解绑VAO和vbo
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    LOGI("VAO initialized successfully");
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_initUBO(JNIEnv *env, jobject thiz) {
    if (gRenderer.program == 0) {
        LOGE("Cannot initialize UBO: program is not created");
        return;
    }
    
    LOGI("Initializing UBO (re-bind after program relink)");
    
    // 重新链接程序后，需要重新创建和绑定UBO
    // 释放旧的 UBO
    if (g_Camera_Uniforms.ubo.ubo != 0) {
        releaseUniformBuffer(&g_Camera_Uniforms.ubo);
    }
    if (g_Particle_Uniforms.ubo.ubo != 0) {
        releaseUniformBuffer(&g_Particle_Uniforms.ubo);
    }
    
    // 重新创建 Camera UBO
    g_Camera_Uniforms.ubo = createUniformBuffer(gRenderer.program, "CameraUniforms", 0);
    updateUniformBuffer(&g_Camera_Uniforms.ubo, &g_Camera_Uniforms.aspectRatio, 0, sizeof(g_Camera_Uniforms.aspectRatio));
    g_Camera_Uniforms.cameraPos[0] = 0.0f;
    g_Camera_Uniforms.cameraPos[1] = 0.0f;
    g_Camera_Uniforms.cameraPos[2] = 0.0f;
    updateUniformBuffer(&g_Camera_Uniforms.ubo, &g_Camera_Uniforms.cameraPos, 16, sizeof(g_Camera_Uniforms.cameraPos));

    // 重新创建 Particle UBO（确保使用相同的初始值）
    g_Particle_Uniforms.ubo = createUniformBuffer(gRenderer.program, "ParticleUniforms", 1);
    g_Particle_Uniforms.currentTime = 0.0f;  // 初始化累积时间
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.deltaTime, 0, sizeof(g_Particle_Uniforms.deltaTime));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.spoutPos, 16, sizeof(g_Particle_Uniforms.spoutPos));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.gravity, 32, sizeof(g_Particle_Uniforms.gravity));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.maxLifeTime, 44, sizeof(g_Particle_Uniforms.maxLifeTime));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.currentTime, 48, sizeof(g_Particle_Uniforms.currentTime));
    
    LOGI("UBO initialized successfully - spoutPos=(%.2f,%.2f,%.2f), gravity=(%.2f,%.2f,%.2f)", 
         g_Particle_Uniforms.spoutPos[0], g_Particle_Uniforms.spoutPos[1], g_Particle_Uniforms.spoutPos[2],
         g_Particle_Uniforms.gravity[0], g_Particle_Uniforms.gravity[1], g_Particle_Uniforms.gravity[2]);
}
void updateParticlesWithTFB() {
    //禁用光栅化（只更新粒子，不渲染，节省性能）
    glEnable(GL_RASTERIZER_DISCARD);

    // 双缓冲 ping-pong：从 currentBuffer 读取，写入到另一个缓冲区
    int readBuffer = gRenderer.currentBuffer;
    int writeBuffer = 1 - gRenderer.currentBuffer;

    // 绑定读取缓冲区到 VAO（作为输入）
    // 注意：VAO 必须已经绑定（在 nativeRender 中）
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.g_tfb[readBuffer]);
    // 更新顶点属性指针指向读取缓冲区（这些设置会保存到当前绑定的 VAO）
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, diameter)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, velocity)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, lifeTime)));
    glEnableVertexAttribArray(3);

    // 绑定写入缓冲区到 Transform Feedback（作为输出）
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, BINDING_POINT_TFB, gRenderer.g_tfb[writeBuffer]);

    //开启TFB捕获模式（图元类型为点）
    glBeginTransformFeedback(GL_POINTS);

    //执行绘制（从 readBuffer 读取，写入到 writeBuffer）
    glDrawArrays(GL_POINTS, 0, gRenderer.particle_count);

    //关闭TFB模式
    glEndTransformFeedback();

    // 等待 Transform Feedback 完成（确保数据写入完成）
    glFlush();

    // 交换缓冲区：下次从 writeBuffer 读取
    gRenderer.currentBuffer = writeBuffer;

    //启用光栅化（后续渲染需要）
    glDisable(GL_RASTERIZER_DISCARD);
}
void renderParticles() {
    // 绑定当前缓冲区（已更新的数据）到 VAO 用于渲染
    // 注意：VAO 已经绑定（在 nativeRender 中），只需要更新 ARRAY_BUFFER 绑定
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.g_tfb[gRenderer.currentBuffer]);
    // 重新设置顶点属性指针（因为缓冲区改变了）
    // VAO 已经绑定，所以这些设置会更新 VAO 的状态
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, diameter)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, velocity)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(offsetof(Particle, lifeTime)));
    glEnableVertexAttribArray(3);

    // 绘制更新后的粒子（使用当前缓冲区中的数据）
    glDrawArrays(GL_POINTS, 0, gRenderer.particle_count);
}

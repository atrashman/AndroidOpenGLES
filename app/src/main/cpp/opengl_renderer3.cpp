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
    GLuint g_tfb;
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
} g_Particle_Uniforms;

static const int BINDING_POINT_TFB =0;
static const int BINDING_POINT_VAO =1;
void updateParticlesWithTFB();

const GLchar* g_TransformFeedbackVaryings[] = {
        "vPosition",
        "vDiameter",
        "vVelocity",
        "vLifetime"
};

void updateParticlesWithTFB() {
    //禁用光栅化（只更新粒子，不渲染，节省性能）
    glEnable(GL_RASTERIZER_DISCARD);

    //绑定TFB缓冲区到绑定点BINDING_POINT_TFB
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, BINDING_POINT_TFB, gRenderer.g_tfb);

    //开启TFB捕获模式（图元类型为点）
    glBeginTransformFeedback(GL_POINTS);

    //执行绘制
    glDrawArrays(GL_POINTS, 0, gRenderer.particle_count);

    //关闭TFB模式
    glEndTransformFeedback();
    //启用光栅化（后续渲染需要）
    glDisable(GL_RASTERIZER_DISCARD);

}
 void renderParticles() {
    // 直接绘制更新后的粒子（TFB缓冲区已存储最新属性）
    glDrawArrays(GL_POINTS, 0, gRenderer.particle_count);
}

// 顶点着色器（简化版，可根据需要修改）
static const char* vertexShaderSource = R"(
#version 300 es

layout (location = 0) in vec3 aPosition;
layout (location = 1) in float diameter;
layout (location = 2) in vec3 aVelocity;
layout (location = 3) in float aLifetime;

layout(std140) uniform CameraUniforms {
        float uAspectRatio;    // 宽高比
        vec3 uCameraPos;   // 相机位置
    };
layout(std140) uniform ParticleUniforms {
        float uDeltaTime; //客观时间
        vec3 uSpoutPos;   // 喷口位置
        vec3 uGravity;  // 重力加速度向量 (0, -9.8, 0) 或类似值
        float uMaxLifeTime;  // 最大生命周期
    };

// 随机函数（GPU生成随机数，基于粒子位置）
highp float random(vec2 co) {
    highp float a = 12.9898;
    highp float b = 78.233;
    highp float c = 43758.5453;
    highp float dt= dot(co.xy ,vec2(a,b));
    highp float sn= mod(dt,3.1415926);
    //返回sin(sn)*c的小数部分，即0-1之间的随机数
    return fract(sin(sn) * c);
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

    if (currentLife <= 0.0f) {
        //生命周期结束，重置粒子
        currentPos = uSpoutPos;
        currentDiameter = random(currentPos.xy) * 0.5f + 0.5f;  // 直径 0.5-1.0
        // 给粒子一个向上的初始速度，加上一些随机性
        vec2 randXY = vec2(random(currentPos.xy), random(currentPos.yx));
        currentVel = vec3(
            (randXY.x - 0.5f) * 0.3f,  // X方向随机速度，减小
            random(currentPos.xy) * 0.8f + 0.5f,  // Y方向向上速度 0.5-1.3
            (randXY.y - 0.5f) * 0.3f   // Z方向随机速度，减小
        );
        currentLife = random(currentPos.xy) * 2.0f + 3.0f;  // 生命周期 3-5秒
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
static const char* fragmentShaderSource = R"(
#version 300 es
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
    // 编译着色器程序
    gRenderer.program = createProgram(vertexShaderSource, fragmentShaderSource);
    gRenderer.particle_count = 1000;
    if (gRenderer.program == 0) {
        LOGE("Failed to create shader program");
        return JNI_FALSE;
    }
    // 开启点精灵（渲染粒子为点）
    glEnable(GL_PROGRAM_POINT_SIZE);
    gRenderer.initialized = true;

    //初始化统一变量ubo
    g_Camera_Uniforms.ubo = createUniformBuffer(gRenderer.program, "CameraUniforms", 0);
    updateUniformBuffer(&g_Camera_Uniforms.ubo, &g_Camera_Uniforms.aspectRatio, 0, sizeof(g_Camera_Uniforms.aspectRatio));


    g_Particle_Uniforms.ubo = createUniformBuffer(gRenderer.program, "ParticleUniforms", 1);
    g_Particle_Uniforms.deltaTime = 0.0f;
    float spoutPosTemp[] = {0.0f, -0.8f, 0.0f};  // 屏幕下方
    float gravityTemp[] = {0.0f, -0.5f, 0.0f};   // 降低重力，粒子飞得更高更慢
    memcpy(g_Particle_Uniforms.spoutPos, spoutPosTemp, sizeof(spoutPosTemp));
    memcpy(g_Particle_Uniforms.gravity, gravityTemp, sizeof(gravityTemp));
    g_Particle_Uniforms.maxLifeTime = MAX_LIFE_TIME;
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.deltaTime, 0, sizeof(g_Particle_Uniforms.deltaTime));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.spoutPos, 16, sizeof(g_Particle_Uniforms.spoutPos));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.gravity, 32, sizeof(g_Particle_Uniforms.gravity));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.maxLifeTime, 44, sizeof(g_Particle_Uniforms.maxLifeTime));
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
    
    if (gRenderer.mesh.vao == 0 || gRenderer.g_tfb == 0) {
        LOGE("VAO or TFB not initialized: vao=%d, tfb=%d", gRenderer.mesh.vao, gRenderer.g_tfb);
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
    //当前时间
    struct timeval tv;
    gettimeofday(&tv, NULL);
    float currentTime = (float)(tv.tv_sec + tv.tv_usec / 1e6);
    float deltaTime;
    if (last_time == 0.0f){
        deltaTime = 0.0f;
    } else {
        deltaTime = currentTime - last_time;
    }
    last_time = currentTime;
    g_Particle_Uniforms.deltaTime = deltaTime;
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.deltaTime, 0, sizeof(g_Particle_Uniforms.deltaTime));
    
    if (frameCount == 1) {
        LOGI("First frame: deltaTime=%.3f, particle_count=%d", deltaTime, gRenderer.particle_count);
    }


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
        } else if (gRenderer.g_tfb != 0){
            //两次 glDrawArrays 看似重复，实则目的完全不同：
            //第一次是 "更新粒子数据"（只跑顶点着色器，不渲染），
            //第二次是 "渲染粒子"（跑完整管线，显示到屏幕）。
            if (frameCount == 1) {
                LOGI("Drawing particles for first time");
            }
            updateParticlesWithTFB();
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
    LOGI("Initializing TFB buffer with %d particles", gRenderer.particle_count);
    
    //create
    glGenBuffers(1, &gRenderer.g_tfb);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, gRenderer.g_tfb);

    //分配大小
    int buffer_size = gRenderer.particle_count * sizeof(Particle);

    // 创建并初始化粒子数据
    Particle* particles = new Particle[gRenderer.particle_count];
    for (int i = 0; i < gRenderer.particle_count; i++) {
        // 初始位置设为喷出点（或任意位置，因为第一帧会重置）
        particles[i].position[0] = 0.0f;
        particles[i].position[1] = 0.0f;
        particles[i].position[2] = 0.0f;
        
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

    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, buffer_size, particles, GL_DYNAMIC_COPY);
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
        GLchar info[512];
        glGetProgramInfoLog(gRenderer.program, 512, nullptr, info);
        LOGE("Program link failed after TFB setup: %s", info);
    } else {
        LOGI("TFB buffer initialized successfully");
    }
    //解绑
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);

}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_initVAO(JNIEnv *env, jobject thiz) {
    LOGI("Initializing VAO");
    
    glGenVertexArrays(1, &gRenderer.mesh.vao);
    glBindVertexArray(gRenderer.mesh.vao);
    //绑定TFB缓冲区作为顶点缓冲区（因为粒子数据存在这里）
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.g_tfb);

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
    
    // 重新创建 Particle UBO（确保使用相同的初始值）
    g_Particle_Uniforms.ubo = createUniformBuffer(gRenderer.program, "ParticleUniforms", 1);
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.deltaTime, 0, sizeof(g_Particle_Uniforms.deltaTime));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.spoutPos, 16, sizeof(g_Particle_Uniforms.spoutPos));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.gravity, 32, sizeof(g_Particle_Uniforms.gravity));
    updateUniformBuffer(&g_Particle_Uniforms.ubo, &g_Particle_Uniforms.maxLifeTime, 44, sizeof(g_Particle_Uniforms.maxLifeTime));
    
    LOGI("UBO initialized successfully - spoutPos=(%.2f,%.2f,%.2f), gravity=(%.2f,%.2f,%.2f)", 
         g_Particle_Uniforms.spoutPos[0], g_Particle_Uniforms.spoutPos[1], g_Particle_Uniforms.spoutPos[2],
         g_Particle_Uniforms.gravity[0], g_Particle_Uniforms.gravity[1], g_Particle_Uniforms.gravity[2]);
}
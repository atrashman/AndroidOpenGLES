//
// Created by zhangx on 2026/1/3.
// OpenGL Renderer 3 - 使用共享工具库的简化版本
//

#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include "opengl_utils.h"

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

const GLchar* g_TransformFeedbackVaryings[] = {
        "vPosition",
        "vDiameter",
        "vVelocity",
        "vLifetime"
};


// 顶点着色器（简化版，可根据需要修改）
static const char* vertexShaderSource = R"(
#version 300 es

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 1.0);
}
)";

// 片段着色器（简化版，可根据需要修改）
static const char* fragmentShaderSource = R"(
#version 300 es
precision mediump float;

uniform sampler2D uTexture;

in vec2 vTexCoord;

out vec4 fragColor;

void main() {
    vec4 textureColor = texture(uTexture, vTexCoord);
    fragColor = textureColor;
}
)";

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
}

// 渲染一帧
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_nativeRender(JNIEnv *env, jobject thiz) {
    if (!gRenderer.initialized || gRenderer.program == 0) {
        LOGE("Renderer not initialized");
        return;
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(gRenderer.program);

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
    // TODO: implement initTFBBuffer()
    //create
    GLuint tfb = 0;
    glGenBuffers(1, &tfb);
    gRenderer.g_tfb = tfb;
    glGenBuffers(1, &gRenderer.g_tfb);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, gRenderer.g_tfb);

    //分配大小
    int buffer_size = gRenderer.particle_count * sizeof(Particle);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, buffer_size, nullptr, GL_DYNAMIC_COPY);

    //指定TFB要捕获的变量
    glTransformFeedbackVaryings(gRenderer.program, sizeof(g_TransformFeedbackVaryings), g_TransformFeedbackVaryings, GL_INTERLEAVED_ATTRIBS);

    // 重新链接着色器程序（使TFB变量设置生效）
    glLinkProgram(gRenderer.program);

    //检查链接状态
    GLint status;
    glGetProgramiv(gRenderer.program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        //抛出错误
        GLchar info[512];
        glGetProgramInfoLog(gRenderer.program, 512, nullptr, info);
    }
    //解绑
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);

}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer3_initVAO(JNIEnv *env, jobject thiz) {
    // TODO: implement initVAO()
}
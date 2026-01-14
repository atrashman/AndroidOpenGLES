//
// Created by zhangx on 2026/1/3.
// OpenGL 通用工具库实现
//

#include "opengl_utils.h"
#include <android/log.h>
#include <android/bitmap.h>
#include <cstring>

#define LOG_TAG "OpenGLUtils"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 编译着色器
GLuint compileShader(GLenum type, const char* source) {
    const char* shaderType = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
    LOGI("Compiling %s shader...", shaderType);
    
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        LOGE("Failed to create %s shader", shaderType);
        return 0;
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char* infoLog = new char[infoLen];
            glGetShaderInfoLog(shader, infoLen, nullptr, infoLog);
            LOGE("%s shader compilation error: %s", shaderType, infoLog);
            delete[] infoLog;
        } else {
            LOGE("%s shader compilation failed (no error log available)", shaderType);
        }
        glDeleteShader(shader);
        return 0;
    }

    LOGI("%s shader compiled successfully", shaderType);
    return shader;
}

// 创建着色器程序
GLuint createProgram(const char* vertexShaderSource, const char* fragmentShaderSource) {
    LOGI("Creating shader program...");
    
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    if (vertexShader == 0) {
        LOGE("Vertex shader compilation failed, aborting program creation");
        return 0;
    }

    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (fragmentShader == 0) {
        LOGE("Fragment shader compilation failed, aborting program creation");
        glDeleteShader(vertexShader);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        LOGE("Failed to create program object");
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    LOGI("Linking shader program...");
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char* infoLog = new char[infoLen];
            glGetProgramInfoLog(program, infoLen, nullptr, infoLog);
            LOGE("Program linking error: %s", infoLog);
            delete[] infoLog;
        } else {
            LOGE("Program linking failed (no error log available)");
        }
        glDeleteProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    LOGI("Shader program created and linked successfully, program ID=%d", program);
    return program;
}

// 从 Bitmap 加载纹理
GLuint loadTextureFromBitmap(JNIEnv* env, jobject bitmap) {
    AndroidBitmapInfo info;
    void *pixels = nullptr;

    // 获取Bitmap信息
    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
        LOGE("Failed to get bitmap info");
        return 0;
    }

    // 仅处理RGBA_8888格式
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("Unsupported bitmap format");
        return 0;
    }

    // 锁定像素内存
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0) {
        LOGE("Failed to lock bitmap pixels");
        return 0;
    }

    // 生成纹理
    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // 上传像素数据
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        info.width,
        info.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels
    );

    // 解锁Bitmap
    AndroidBitmap_unlockPixels(env, bitmap);

    // 设置纹理参数
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    return textureID;
}

// 释放纹理
void releaseTexture(GLuint textureID) {
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
    }
}

// 创建网格数据
MeshData createMesh(const float* vertices, size_t vertexCount, size_t vertexSize,
                    const unsigned int* indices, size_t indexCount,
                    const int* attribSizes, size_t attribCount) {
    MeshData mesh = {0, 0, 0, 0};

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    // 上传顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * vertexSize * sizeof(float), vertices, GL_STATIC_DRAW);

    // 设置顶点属性
    size_t offset = 0;
    for (size_t i = 0; i < attribCount; i++) {
        glVertexAttribPointer(i, attribSizes[i], GL_FLOAT, GL_FALSE,
                             vertexSize * sizeof(float), (void*)(offset * sizeof(float)));
        glEnableVertexAttribArray(i);
        offset += attribSizes[i];
    }

    // 上传索引数据
    if (indices != nullptr && indexCount > 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(unsigned int), indices, GL_STATIC_DRAW);
        mesh.indexCount = indexCount;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return mesh;
}

// 释放网格数据
void releaseMesh(MeshData* mesh) {
    if (mesh == nullptr) return;

    if (mesh->vao != 0) {
        glDeleteVertexArrays(1, &mesh->vao);
        mesh->vao = 0;
    }
    if (mesh->vbo != 0) {
        glDeleteBuffers(1, &mesh->vbo);
        mesh->vbo = 0;
    }
    if (mesh->ebo != 0) {
        glDeleteBuffers(1, &mesh->ebo);
        mesh->ebo = 0;
    }
    mesh->indexCount = 0;
}

// 创建 Uniform Buffer Object
UniformBuffer createUniformBuffer(GLuint program, const char* blockName, GLuint bindingPoint) {
    UniformBuffer ubo = {0, bindingPoint, 0};

    GLuint blockIndex = glGetUniformBlockIndex(program, blockName);
    if (blockIndex == GL_INVALID_INDEX) {
        LOGE("Uniform block '%s' not found in shader", blockName);
        return ubo;
    }

    // 绑定到绑定点
    glUniformBlockBinding(program, blockIndex, bindingPoint);

    // 获取块大小
    GLint blockSize = 0;
    glGetActiveUniformBlockiv(program, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);

    if (blockSize > 0) {
        glGenBuffers(1, &ubo.ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo.ubo);
        glBufferData(GL_UNIFORM_BUFFER, blockSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, ubo.ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        ubo.size = blockSize;
    }

    return ubo;
}

// 更新 Uniform Buffer Object
void updateUniformBuffer(UniformBuffer* ubo, const void* data, size_t offset, size_t size) {
    if (ubo == nullptr || ubo->ubo == 0) {
        LOGE("Invalid UBO");
        return;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, ubo->ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

// 释放 Uniform Buffer Object
void releaseUniformBuffer(UniformBuffer* ubo) {
    if (ubo == nullptr) return;

    if (ubo->ubo != 0) {
        glDeleteBuffers(1, &ubo->ubo);
        ubo->ubo = 0;
    }
    ubo->size = 0;
}

// 记录 OpenGL 错误
void logGLError(const char* tag, const char* operation) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOGE("[%s] %s failed with error: 0x%x", tag, operation, error);
    }
}


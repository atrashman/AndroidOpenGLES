//
// Created by zhangx on 2026/1/3.
// OpenGL 通用工具库 - 可复用的 OpenGL 功能
//

#ifndef NDKLEARN2_OPENGL_UTILS_H
#define NDKLEARN2_OPENGL_UTILS_H

#include <GLES3/gl3.h>
#include <jni.h>
#include <string>
#ifdef __cplusplus
extern "C" {
#endif

// 着色器编译
GLuint compileShader(GLenum type, const char* source);
GLuint createProgram(const char* vertexShaderSource, const char* fragmentShaderSource);

// 纹理管理
GLuint loadTextureFromBitmap(JNIEnv* env, jobject bitmap);
void releaseTexture(GLuint textureID);

// VAO/VBO/EBO 管理
typedef struct {
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLsizei indexCount;
} MeshData;

MeshData createMesh(const float* vertices, size_t vertexCount, size_t vertexSize,
                    const unsigned int* indices, size_t indexCount,
                    const int* attribSizes, size_t attribCount);
void releaseMesh(MeshData* mesh);

// UBO 管理
typedef struct {
    GLuint ubo;
    GLuint bindingPoint;
    GLsizeiptr size;
} UniformBuffer;

UniformBuffer createUniformBuffer(GLuint program, const char* blockName, GLuint bindingPoint);
void updateUniformBuffer(UniformBuffer* ubo, const void* data, size_t offset, size_t size);
void releaseUniformBuffer(UniformBuffer* ubo);

// 辅助函数
void logGLError(const char* tag, const char* operation);

//其他数据结构
typedef struct {
    float position[3];
    float diameter;
    float velocity[3];
    float lifeTime;//粒子剩余生命周期（秒）,<=0 时重置粒子
} Particle;
const int GL_PROGRAM_POINT_SIZE = 0x8642;

#ifdef __cplusplus
}
#endif

#endif //NDKLEARN2_OPENGL_UTILS_H


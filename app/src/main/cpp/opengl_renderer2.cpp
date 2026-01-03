//
// Created by zhangx on 2026/1/3.
//

#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <cmath>

#define LOG_TAG "OpenGLRenderer2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void compileShader(GLenum type, const char* source);
static GLuint createProgram();

static GLuint gProgram = 0;
static GLuint gVAO = 0;
static GLuint gVBO = 0;

//顶点着色器
static const char* vertexShaderSource = R"(
#version 300 es

uniform vec3 lightLoc;
uniform vec3 lightStrength;
uniform mat4 uModelMatrix;      // 模型矩阵
uniform mat4 uViewMatrix;       // 视图矩阵
uniform mat4 uProjectionMatrix; // 投影矩阵
uniform mat3 uNormalMatrix;     // 法线矩阵（用于变换法线）

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;//顶点法向量
layout(location = 2) in vec2 aTexCoord;    // 纹理坐标（可选）


out vec3 worldPos;//因为光照要通过世界坐标
out vec3 vWorldSpaceNormal;
out vec2 vTexCoord;      // 纹理坐标
void main() {

    worldPos = (uModelMatrix * aPosition).xyz;

    vTexCoord = aTexCoord;

    vWorldSpaceNormal = normalize(uNormalMatrix * aNormal);

    // gl_Position 是内置变量，必须设置！
    // 这是顶点在裁剪空间中的最终位置
    gl_Position = uProjectionMatrix * uViewMatrix * worldPos;
}
)";

static const char* fragmentShaderSource = R"(
#version 300 es
precision mediump float;

uniform vec3 eyePos;

// 光照颜色
uniform vec3 uAmbientColor;   // 环境光颜色
uniform vec3 uDiffuseColor;  // 漫反射光颜色
uniform vec3 uSpecularColor; // 镜面反射光颜色

//光照方向
uniform vec3 uLightDirection;

// 光照距离衰减因子
uniform vec3 uAttenuationFactors;  // (K0, K1, K2)
uniform bool uComputeDistanceAttenuation;  // 是否计算距离衰减

// 聚光灯参数
uniform vec3 uSpotDirection;        // 聚光灯方向（归一化）
uniform float uSpotExponent;        // 聚光灯指数
uniform float uSpotCutoffAngle;     // 聚光灯截止角度（度数）

// 材质属性
uniform vec3 uMaterialAmbient;      // 材质环境光反射系数
uniform vec3 uMaterialDiffuse;     // 材质漫反射系数
uniform vec3 uMaterialSpecular;    // 材质镜面反射系数
uniform float uMaterialShininess;  // 材质光泽度

// 相机位置
uniform vec3 uCameraPos;

// 光源位置
uniform vec3 uLightPos;

in vec3 vWorldPos;      // 世界空间位置
in vec3 vWorldSpaceNormal;        // 世界空间法线
in vec2 vTexCoord;     // 纹理坐标


// 输出：最终像素颜色
out vec4 fragColor;  // 输出到帧缓冲区的颜色

void main() {
    // 计算光线方向
    vec3 L;
    float distance = 0.0;
    float attenuation = 1.0;

    //方向光是完整覆盖图形的平行光柱 与之相对的是点光源
    // 判断是方向光还是点光源/聚光灯
    if (uLightDirection.x == 0.0 && uLightDirection.y == 0.0 && uLightDirection.z == 0.0) {
        //点光源情况
        //计算点光源到当前fragment的位置
        //Vec3 fragCenter = vWorldPos+0.5; 错误 因为是在三维中的
        vec3 lightDir = uLightPos - vWorldPos;
        distance = length(lightDir);
        L = normalize(lightDir);


        // 计算点光源距离衰减
        if (uComputeDistanceAttenuation) {
            float K0 = uAttenuationFactors.x;  // 常数项
            float K1 = uAttenuationFactors.y;  // 线性项
            float K2 = uAttenuationFactors.z;  // 二次项
            attenuation = 1.0 / (K0 + K1 * distance + K2 * distance * distance);
        }

        //计算聚光灯影响
        float spotEffect = 1.0;
        if (uSpotCutoffAngle > 0.0 && uSpotCutoffAngle < 90.0) {
            // 计算聚光灯效果
            vec3 spotDir = normalize(uSpotDirection);
            float cosAngle = dot(-L, spotDir);
            float cutoff = cos(radians(uSpotCutoffAngle));

            if (cosAngle > cutoff) {
                // 在聚光灯范围内
                spotEffect = pow(cosAngle, uSpotExponent);
            } else {
                // 在聚光灯范围外
                spotEffect = 0.0;
            }

        }
        attenuation *= spotEffect;
    } else {
        //平行光
        
    }
}
)";




extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_nativeInit(JNIEnv* env, jobject thiz) {
    LOGI("Initializing Lighting");

    //编译着色器
    gProgram = createProgram();
    if (gProgram == 0) {
        LOGE("Failed to create shader program");
        return JNI_FALSE;
    }

}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_nativeResize(JNIEnv *env, jobject thiz, jint width,
                                                        jint height) {
    // TODO: implement nativeResize()
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_nativeRender(JNIEnv *env, jobject thiz) {
    // TODO: implement nativeRender()
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_nativeCleanup(JNIEnv *env, jobject thiz) {
    // TODO: implement nativeCleanup()
}


static GLuint createProgram() {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    if (vertexShader == 0) {
        return 0;
    }

    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        LOGE("Failed to create program");
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
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
        }
        glDeleteProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}
static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        LOGE("Failed to create shader");
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
            LOGE("Shader compilation error: %s", infoLog);
            delete[] infoLog;
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}
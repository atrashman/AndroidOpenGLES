//
// Created by zhangx on 2026/1/3.
//

#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <cmath>
#include <android/bitmap.h>

#define LOG_TAG "OpenGLRenderer2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void compileShader(GLenum type, const char* source);
static GLuint createProgram();

static GLuint gProgram = 0;
static GLuint gLightingProgram = 0;  // 光照程序
static GLuint gVAO = 0;
static GLuint gVBO = 0;
static GLuint gEBO = 0;
static GLuint gTextureID1 = 0;
static GLuint g_textureID = 0;  // 纹理ID

// Uniform Buffer Objects
static GLuint gUBOTransform = 0;   // 变换矩阵UBO
static GLuint gUBOLight = 0;        // 光照UBO
static GLuint gUBOMaterial = 0;     // 材质UBO

// Uniform Block 绑定点
const GLuint UBO_BINDING_TRANSFORM = 0;
const GLuint UBO_BINDING_LIGHT = 1;
const GLuint UBO_BINDING_MATERIAL = 2;



//顶点着色器
static const char* vertexShaderSource = R"(
#version 300 es

// 变换矩阵 Uniform Block
layout(std140) uniform TransformBlock {
    mat4 uModelMatrix;      // 模型矩阵
    mat4 uViewMatrix;       // 视图矩阵
    mat4 uProjectionMatrix; // 投影矩阵
    mat3 uNormalMatrix;     // 法线矩阵（用于变换法线）
};

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;//顶点法向量
layout(location = 2) in vec2 aTexCoord;    // 纹理坐标（可选）


out vec3 worldPos;//因为光照要通过世界坐标
out vec3 vWorldSpaceNormal;
out vec2 vTexCoord;      // 纹理坐标
void main() {

    worldPos = (uModelMatrix * vec4(aPosition, 1.0)).xyz;

    vTexCoord = aTexCoord;

    vWorldSpaceNormal = normalize(uNormalMatrix * aNormal);

    // gl_Position 是内置变量，必须设置！
    // 这是顶点在裁剪空间中的最终位置
    gl_Position = uProjectionMatrix * uViewMatrix * vec4(worldPos, 1.0);
}
)";

static const char* fragmentShaderSource = R"(
#version 300 es
precision mediump float;

// 光照 Uniform Block
layout(std140) uniform LightBlock {
    vec3 uAmbientColor;           // 环境光颜色
    vec3 uDiffuseColor;            // 漫反射光颜色
    vec3 uSpecularColor;           // 镜面反射光颜色
    vec3 uLightDirection;          // 光照方向
    vec3 uLightPos;                // 光源位置
    vec3 uAttenuationFactors;      // 距离衰减因子 (K0, K1, K2)
    float uSpotExponent;           // 聚光灯指数
    float uSpotCutoffAngle;       // 聚光灯截止角度（度数）
    vec3 uSpotDirection;           // 聚光灯方向（归一化）
    int uComputeDistanceAttenuation;  // 是否计算距离衰减 (bool用int表示)
};

// 材质 Uniform Block
layout(std140) uniform MaterialBlock {
    vec3 uMaterialAmbient;         // 材质环境光反射系数
    vec3 uMaterialDiffuse;         // 材质漫反射系数
    vec3 uMaterialSpecular;        // 材质镜面反射系数
    float uMaterialShininess;     // 材质光泽度
};

// 相机位置（单独uniform，因为可能频繁变化）
uniform vec3 uCameraPos;

in vec3 vWorldPos;                // 世界空间位置
in vec3 vWorldSpaceNormal;         // 世界空间法线
in vec2 vTexCoord;                 // 纹理坐标

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
        if (uComputeDistanceAttenuation != 0) {
            float K0 = uAttenuationFactors.x;  // 常数项
            float K1 = uAttenuationFactors.y;  // 线性项
            float K2 = uAttenuationFactors.z;  // 二次项
            attenuation = 1.0 / (K0 + K1 * distance + K2 * distance * distance);
        }

        //计算聚光灯范围影响
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
        L = normalize(-uLightDirection);
        attenuation = 1.0;  // 方向光无衰减
    }

    // ========== Phong 光照模型 ==========
    vec3 N = normalize(vWorldSpaceNormal);

    // 1. 环境光
    vec3 ambient = uAmbientColor * uMaterialAmbient;

    // 2. 漫反射光
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = uDiffuseColor * uMaterialDiffuse * NdotL;

    // 3. 镜面反射光
    vec3 specular = vec3(0.0);
    if (NdotL > 0.0) {
        // 计算视线方向
        vec3 V = normalize(uCameraPos - vWorldPos);

        // 计算反射方向
        vec3 R = reflect(-L, N);

        // 计算镜面反射
        float RdotV = max(dot(R, V), 0.0);
        specular = uSpecularColor * uMaterialSpecular * pow(RdotV, uMaterialShininess);
    }

    // 4. 最终颜色
    vec3 finalColor = ambient + (diffuse + specular) * attenuation;

    fragColor = vec4(finalColor, 1.0);

}
)";




extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_nativeInit(JNIEnv* env, jobject thiz) {
    LOGI("Initializing Lighting");

    //编译着色器
    gProgram = createProgram();
    gLightingProgram = gProgram;  // 使用同一个程序
    if (gProgram == 0) {
        LOGE("Failed to create shader program");
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_loadTextureFromBitmap(JNIEnv *env, jobject thiz, jobject bitmap) {
    AndroidBitmapInfo info;
    void *pixels = nullptr;

    // 1. 获取Bitmap信息
    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
        return;
    }

    // 仅处理ARGB_8888格式（其他格式需额外适配）
    if (info.format != ANDROID_BITMAP_FORMAT_ARGB_8888) {
        return;
    }

    // 2. 锁定像素内存
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0) {
        return;
    }

    // 3. 生成并绑定纹理对象（核心修复1）
    if (g_textureID == 0) {
        glGenTextures(1, &g_textureID); // 生成纹理ID
    }
    glBindTexture(GL_TEXTURE_2D, g_textureID); // 绑定到当前纹理单元

    // 4. 转换ARGB→RGBA并上传（核心修复2）
    int width = info.width;
    int height = info.height;
    uint8_t *srcPixels = static_cast<uint8_t*>(pixels);
    uint8_t *rgbaPixels = new uint8_t[width * height * 4]; // 临时存储RGBA数据

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = (y * width + x) * 4;
            int dstIdx = (y * width + x) * 4;
            // ARGB → RGBA 字节交换
            rgbaPixels[dstIdx + 0] = srcPixels[srcIdx + 1]; // R
            rgbaPixels[dstIdx + 1] = srcPixels[srcIdx + 2]; // G
            rgbaPixels[dstIdx + 2] = srcPixels[srcIdx + 3]; // B
            rgbaPixels[dstIdx + 3] = srcPixels[srcIdx + 0]; // A
        }
    }

    // 上传转换后的RGBA数据到OpenGL
    glTexImage2D(
            GL_TEXTURE_2D,        // 2D纹理
            0,                    // 基础mip层级
            GL_RGBA,              // GPU内部格式
            width,                // 宽度
            height,               // 高度
            0,                    // 无边框
            GL_RGBA,              // 输入数据格式
            GL_UNSIGNED_BYTE,     // 数据类型
            rgbaPixels            // 转换后的RGBA像素
    );

    // 释放临时内存
    delete[] rgbaPixels;

    // 5. 解锁Bitmap像素
    AndroidBitmap_unlockPixels(env, bitmap);

    // 6. 设置完整的纹理参数（核心修复3）
    // 过滤模式（解决黑屏/模糊问题）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 环绕模式（适配非2次幂尺寸）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 生成mipmap（提升缩放效果）
    glGenerateMipmap(GL_TEXTURE_2D);
}
extern "C"
JNIEXPORT void JNICALL
        Java_com_example_ndklearn2_OpenGLRenderer2_releaseTexture(JNIEnv *env, jobject thiz) {
if (g_textureID != 0) {
glDeleteTextures(1, &g_textureID);
g_textureID = 0;
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
    // 清理VAO, VBO, EBO
    if (gVAO != 0) {
        glDeleteVertexArrays(1, &gVAO);
        gVAO = 0;
    }
    if (gVBO != 0) {
        glDeleteBuffers(1, &gVBO);
        gVBO = 0;
    }
    if (gEBO != 0) {
        glDeleteBuffers(1, &gEBO);
        gEBO = 0;
    }
    
    // 清理UBO
    if (gUBOTransform != 0) {
        glDeleteBuffers(1, &gUBOTransform);
        gUBOTransform = 0;
    }
    if (gUBOLight != 0) {
        glDeleteBuffers(1, &gUBOLight);
        gUBOLight = 0;
    }
    if (gUBOMaterial != 0) {
        glDeleteBuffers(1, &gUBOMaterial);
        gUBOMaterial = 0;
    }
    
    // 清理着色器程序
    if (gProgram != 0) {
        glDeleteProgram(gProgram);
        gProgram = 0;
        gLightingProgram = 0;
    }
    
    // 清理纹理
    if (g_textureID != 0) {
        glDeleteTextures(1, &g_textureID);
        g_textureID = 0;
    }
    if (gTextureID1 != 0) {
        glDeleteTextures(1, &gTextureID1);
        gTextureID1 = 0;
    }
    
    LOGI("Resources cleaned up");
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


extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_loadVertice(JNIEnv *env, jobject thiz) {
    // TODO: implement loadVertice()
    float vertices[] = {
            // 位置              // 法线            // UV      // 面ID（float转int）
            // 纹理布局：三行两列，每个矩形宽0.5，高1/3
            // 第1行(底部): [0.0-0.5, 0.0-1/3] [0.5-1.0, 0.0-1/3]
            // 第2行(中间): [0.0-0.5, 1/3-2/3] [0.5-1.0, 1/3-2/3]
            // 第3行(顶部): [0.0-0.5, 2/3-1.0] [0.5-1.0, 2/3-1.0]
            
            // 负Z面 (ID 5) -> 第1行第2列: U[0.5-1.0], V[0.0-1/3]
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.5f, 0.0f, 5.0f,
            0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f, 5.0f,
            0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.333333f, 5.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.5f, 0.333333f, 5.0f,

            // 正Z面 (ID 2) -> 第2行第1列: U[0.0-0.5], V[1/3-2/3]
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.333333f, 2.0f,
            0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.5f, 0.333333f, 2.0f,
            0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.5f, 0.666667f, 2.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.666667f, 2.0f,

            // 负X面 (ID 3) -> 第2行第2列: U[0.5-1.0], V[1/3-2/3]
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.5f, 0.666667f, 3.0f,
            -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.666667f, 3.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.333333f, 3.0f,
            -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.5f, 0.333333f, 3.0f,

            // 正X面 (ID 0) -> 第3行第1列: U[0.0-0.5], V[2/3-1.0]
            0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.666667f, 0.0f,
            0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.5f, 0.666667f, 0.0f,
            0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.5f, 1.0f, 0.0f,
            0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f, 0.0f,

            // 负Y面 (ID 4) -> 第1行第1列: U[0.0-0.5], V[0.0-1/3]
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f, 4.0f,
            0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.5f, 0.0f, 4.0f,
            0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.5f, 0.333333f, 4.0f,
            -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.333333f, 4.0f,

            // 正Y面 (ID 1) -> 第3行第2列: U[0.5-1.0], V[2/3-1.0]
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.5f, 0.666667f, 1.0f,
            0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.666667f, 1.0f,
            0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f, 1.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.5f, 1.0f, 1.0f
    };
    //利用index来组成正方体
    unsigned int indices[] = {
        0,1,2, 2,3,0,      // 负Z面
        4,5,6, 6,7,4,      // 正Z面
        8,9,10, 10,11,8,   // 负X面
        12,13,14, 14,15,12, // 正X面
        16,17,18, 18,19,16, // 负Y面
        20,21,22, 22,23,20  // 正Y面
    };


    // 创建 VAO 和 VBO
    glGenVertexArrays(1, &gVAO);
    glGenBuffers(1, &gVBO);
    glGenBuffers(1, &gEBO);
    
    glBindVertexArray(gVAO);
    
    // 绑定并上传顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, gVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    //location 0 vertice
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    //location 1 normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    //location 2 texCoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    //location 3 faceID
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    
    // 绑定并上传索引数据（必须在VAO绑定时绑定EBO）
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    // 注意：不要解绑EBO，因为它已经存储在VAO中了
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_loadUniform(JNIEnv *env, jobject thiz) {
    if (gLightingProgram == 0) {
        LOGE("Shader program not initialized");
        return;
    }

    glUseProgram(gLightingProgram);

    // 获取 Uniform Block 索引
    GLuint transformBlockIndex = glGetUniformBlockIndex(gLightingProgram, "TransformBlock");
    GLuint lightBlockIndex = glGetUniformBlockIndex(gLightingProgram, "LightBlock");
    GLuint materialBlockIndex = glGetUniformBlockIndex(gLightingProgram, "MaterialBlock");

    // 绑定 Uniform Block 到绑定点
    if (transformBlockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(gLightingProgram, transformBlockIndex, UBO_BINDING_TRANSFORM);
        LOGI("TransformBlock bound to binding point %d", UBO_BINDING_TRANSFORM);
    } else {
        LOGE("TransformBlock not found in shader");
    }

    if (lightBlockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(gLightingProgram, lightBlockIndex, UBO_BINDING_LIGHT);
        LOGI("LightBlock bound to binding point %d", UBO_BINDING_LIGHT);
    } else {
        LOGE("LightBlock not found in shader");
    }

    if (materialBlockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(gLightingProgram, materialBlockIndex, UBO_BINDING_MATERIAL);
        LOGI("MaterialBlock bound to binding point %d", UBO_BINDING_MATERIAL);
    } else {
        LOGE("MaterialBlock not found in shader");
    }

    // 创建 Uniform Buffer Objects
    glGenBuffers(1, &gUBOTransform);
    glGenBuffers(1, &gUBOLight);
    glGenBuffers(1, &gUBOMaterial);

    // 获取 Uniform Block 大小
    GLint transformBlockSize = 0;
    GLint lightBlockSize = 0;
    GLint materialBlockSize = 0;

    if (transformBlockIndex != GL_INVALID_INDEX) {
        glGetActiveUniformBlockiv(gLightingProgram, transformBlockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &transformBlockSize);
        LOGI("TransformBlock size: %d bytes", transformBlockSize);
    }

    if (lightBlockIndex != GL_INVALID_INDEX) {
        glGetActiveUniformBlockiv(gLightingProgram, lightBlockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &lightBlockSize);
        LOGI("LightBlock size: %d bytes", lightBlockSize);
    }

    if (materialBlockIndex != GL_INVALID_INDEX) {
        glGetActiveUniformBlockiv(gLightingProgram, materialBlockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &materialBlockSize);
        LOGI("MaterialBlock size: %d bytes", materialBlockSize);
    }

    // 分配并绑定缓冲区
    if (gUBOTransform != 0 && transformBlockSize > 0) {
        glBindBuffer(GL_UNIFORM_BUFFER, gUBOTransform);
        glBufferData(GL_UNIFORM_BUFFER, transformBlockSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_TRANSFORM, gUBOTransform);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    if (gUBOLight != 0 && lightBlockSize > 0) {
        glBindBuffer(GL_UNIFORM_BUFFER, gUBOLight);
        glBufferData(GL_UNIFORM_BUFFER, lightBlockSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_LIGHT, gUBOLight);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    if (gUBOMaterial != 0 && materialBlockSize > 0) {
        glBindBuffer(GL_UNIFORM_BUFFER, gUBOMaterial);
        glBufferData(GL_UNIFORM_BUFFER, materialBlockSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_MATERIAL, gUBOMaterial);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    glUseProgram(0);
    LOGI("Uniform blocks initialized successfully");
}

// 辅助函数：更新变换矩阵UBO
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_updateTransformUBO(JNIEnv *env, jobject thiz,
    jfloatArray modelMatrix, jfloatArray viewMatrix, jfloatArray projectionMatrix, jfloatArray normalMatrix) {
    if (gUBOTransform == 0) {
        LOGE("Transform UBO not initialized");
        return;
    }

    jfloat* model = env->GetFloatArrayElements(modelMatrix, nullptr);
    jfloat* view = env->GetFloatArrayElements(viewMatrix, nullptr);
    jfloat* proj = env->GetFloatArrayElements(projectionMatrix, nullptr);
    jfloat* normal = env->GetFloatArrayElements(normalMatrix, nullptr);

    glBindBuffer(GL_UNIFORM_BUFFER, gUBOTransform);
    
    // std140布局：mat4占用16个float（4个vec4），每个vec4对齐到16字节
    // 偏移量：modelMatrix(0), viewMatrix(64), projectionMatrix(128), normalMatrix(192)
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 16 * sizeof(float), model);
    glBufferSubData(GL_UNIFORM_BUFFER, 64, 16 * sizeof(float), view);
    glBufferSubData(GL_UNIFORM_BUFFER, 128, 16 * sizeof(float), proj);
    glBufferSubData(GL_UNIFORM_BUFFER, 192, 12 * sizeof(float), normal);  // mat3占用12个float
    
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    env->ReleaseFloatArrayElements(modelMatrix, model, JNI_ABORT);
    env->ReleaseFloatArrayElements(viewMatrix, view, JNI_ABORT);
    env->ReleaseFloatArrayElements(projectionMatrix, proj, JNI_ABORT);
    env->ReleaseFloatArrayElements(normalMatrix, normal, JNI_ABORT);
}

// 辅助函数：更新光照UBO
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_updateLightUBO(JNIEnv *env, jobject thiz,
    jfloatArray ambientColor, jfloatArray diffuseColor, jfloatArray specularColor,
    jfloatArray lightDirection, jfloatArray lightPos, jfloatArray attenuationFactors,
    jfloat spotExponent, jfloat spotCutoffAngle, jfloatArray spotDirection, jint computeDistanceAttenuation) {
    if (gUBOLight == 0) {
        LOGE("Light UBO not initialized");
        return;
    }

    jfloat* ambient = env->GetFloatArrayElements(ambientColor, nullptr);
    jfloat* diffuse = env->GetFloatArrayElements(diffuseColor, nullptr);
    jfloat* specular = env->GetFloatArrayElements(specularColor, nullptr);
    jfloat* lightDir = env->GetFloatArrayElements(lightDirection, nullptr);
    jfloat* lightP = env->GetFloatArrayElements(lightPos, nullptr);
    jfloat* atten = env->GetFloatArrayElements(attenuationFactors, nullptr);
    jfloat* spotDir = env->GetFloatArrayElements(spotDirection, nullptr);

    glBindBuffer(GL_UNIFORM_BUFFER, gUBOLight);
    
    // std140布局：vec3对齐到16字节（4个float），float对齐到4字节
    // 偏移量计算（按std140规则）：
    // ambientColor(0), diffuseColor(16), specularColor(32), lightDirection(48), lightPos(64)
    // attenuationFactors(80), spotExponent(92), spotCutoffAngle(96), spotDirection(112), computeDistanceAttenuation(128)
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 3 * sizeof(float), ambient);
    glBufferSubData(GL_UNIFORM_BUFFER, 16, 3 * sizeof(float), diffuse);
    glBufferSubData(GL_UNIFORM_BUFFER, 32, 3 * sizeof(float), specular);
    glBufferSubData(GL_UNIFORM_BUFFER, 48, 3 * sizeof(float), lightDir);
    glBufferSubData(GL_UNIFORM_BUFFER, 64, 3 * sizeof(float), lightP);
    glBufferSubData(GL_UNIFORM_BUFFER, 80, 3 * sizeof(float), atten);
    glBufferSubData(GL_UNIFORM_BUFFER, 92, sizeof(float), &spotExponent);
    glBufferSubData(GL_UNIFORM_BUFFER, 96, sizeof(float), &spotCutoffAngle);
    glBufferSubData(GL_UNIFORM_BUFFER, 112, 3 * sizeof(float), spotDir);
    glBufferSubData(GL_UNIFORM_BUFFER, 128, sizeof(int), &computeDistanceAttenuation);
    
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    env->ReleaseFloatArrayElements(ambientColor, ambient, JNI_ABORT);
    env->ReleaseFloatArrayElements(diffuseColor, diffuse, JNI_ABORT);
    env->ReleaseFloatArrayElements(specularColor, specular, JNI_ABORT);
    env->ReleaseFloatArrayElements(lightDirection, lightDir, JNI_ABORT);
    env->ReleaseFloatArrayElements(lightPos, lightP, JNI_ABORT);
    env->ReleaseFloatArrayElements(attenuationFactors, atten, JNI_ABORT);
    env->ReleaseFloatArrayElements(spotDirection, spotDir, JNI_ABORT);
}

// 辅助函数：更新材质UBO
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_updateMaterialUBO(JNIEnv *env, jobject thiz,
    jfloatArray materialAmbient, jfloatArray materialDiffuse, jfloatArray materialSpecular, jfloat materialShininess) {
    if (gUBOMaterial == 0) {
        LOGE("Material UBO not initialized");
        return;
    }

    jfloat* ambient = env->GetFloatArrayElements(materialAmbient, nullptr);
    jfloat* diffuse = env->GetFloatArrayElements(materialDiffuse, nullptr);
    jfloat* specular = env->GetFloatArrayElements(materialSpecular, nullptr);

    glBindBuffer(GL_UNIFORM_BUFFER, gUBOMaterial);
    
    // std140布局：vec3对齐到16字节，float对齐到4字节
    // 偏移量：materialAmbient(0), materialDiffuse(16), materialSpecular(32), materialShininess(44)
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 3 * sizeof(float), ambient);
    glBufferSubData(GL_UNIFORM_BUFFER, 16, 3 * sizeof(float), diffuse);
    glBufferSubData(GL_UNIFORM_BUFFER, 32, 3 * sizeof(float), specular);
    glBufferSubData(GL_UNIFORM_BUFFER, 44, sizeof(float), &materialShininess);
    
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    env->ReleaseFloatArrayElements(materialAmbient, ambient, JNI_ABORT);
    env->ReleaseFloatArrayElements(materialDiffuse, diffuse, JNI_ABORT);
    env->ReleaseFloatArrayElements(materialSpecular, specular, JNI_ABORT);
}

// 辅助函数：更新相机位置（单独的uniform）
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer2_updateCameraPos(JNIEnv *env, jobject thiz, jfloatArray cameraPos) {
    if (gLightingProgram == 0) {
        LOGE("Shader program not initialized");
        return;
    }

    jfloat* pos = env->GetFloatArrayElements(cameraPos, nullptr);
    
    glUseProgram(gLightingProgram);
    GLint cameraPosLoc = glGetUniformLocation(gLightingProgram, "uCameraPos");
    if (cameraPosLoc != -1) {
        glUniform3fv(cameraPosLoc, 1, pos);
    }
    glUseProgram(0);

    env->ReleaseFloatArrayElements(cameraPos, pos, JNI_ABORT);
}
#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <cmath>

#define LOG_TAG "OpenGLRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ============================================================
// GLSL 着色器代码 - 这是你学习 GLSL 的核心部分！
// ============================================================
// 
// 【重要】GLSurfaceView → OpenGL → GLSL 的调用流程：
// 1. MainActivity 创建 GLSurfaceView
// 2. GLSurfaceView 调用 OpenGLRenderer.onSurfaceCreated()
// 3. onSurfaceCreated() 调用 nativeInit() (JNI)
// 4. nativeInit() 编译这些 GLSL 着色器代码
// 5. GLSurfaceView 每帧调用 onDrawFrame()
// 6. onDrawFrame() 调用 nativeRender() (JNI)
// 7. nativeRender() 使用编译好的着色器程序绘制
// 8. GLSL 着色器在 GPU 上执行，渲染结果显示在屏幕上
//
// ============================================================

// 顶点着色器源码 (Vertex Shader)
// 作用：处理每个顶点的位置和属性
// 执行时机：每个顶点执行一次
static const char* vertexShaderSource = R"(
#version 300 es
    
    // 输入：顶点属性（从 CPU 传入）
    layout(location = 0) in vec4 aPosition;  // 顶点位置 (x, y, z, w)
    layout(location = 1) in vec4 aColor;     // 顶点颜色 (r, g, b, a)
    
    // 输出：传递给片段着色器的数据
    out vec4 vColor;  // 将颜色传递给片段着色器
    
    void main() {
        // gl_Position 是内置变量，必须设置！
        // 这是顶点在裁剪空间中的最终位置
        gl_Position = aPosition;
        
        // 将颜色传递给片段着色器
        vColor = aColor;
    }
)";

// 片段着色器源码 (Fragment Shader / Pixel Shader)
// 作用：决定每个像素的最终颜色
// 执行时机：每个像素执行一次（在光栅化之后）
static const char* fragmentShaderSource = R"(
#version 300 es
precision mediump float;
    
    // 输入：从顶点着色器传来的数据（会被插值）
    in vec4 vColor;  // 从顶点着色器传来的颜色
    
    // 输出：最终像素颜色
    out vec4 fragColor;  // 输出到帧缓冲区的颜色
    
    void main() {
        // 设置像素颜色
        // 你可以在这里修改颜色来学习 GLSL！
        // 例如：fragColor = vec4(1.0, 0.0, 0.0, 1.0); // 纯红色
        fragColor = vColor;
    }
)";

// ============================================================
// 编译 GLSL 着色器代码
// ============================================================
// 这个函数将 GLSL 字符串源码编译成 GPU 可执行的着色器对象
// type: GL_VERTEX_SHADER 或 GL_FRAGMENT_SHADER
// source: GLSL 源码字符串（就是上面定义的 vertexShaderSource 或 fragmentShaderSource）
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

// ============================================================
// 创建着色器程序（Shader Program）
// ============================================================
// 将顶点着色器和片段着色器链接成一个完整的着色器程序
// 这个程序会在 GPU 上执行，处理所有的渲染工作
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

// 全局变量
static GLuint gProgram = 0;
static GLuint gVAO = 0;
static GLuint gVBO = 0;
static float gRotationAngle = 0.0f;

// ============================================================
// 【关键函数】初始化 OpenGL 和编译 GLSL 着色器
// ============================================================
// 调用时机：GLSurfaceView 创建 OpenGL 上下文后，调用 onSurfaceCreated() 时
// 
// 完整调用链：
// MainActivity.onCreate() 
//   → 创建 GLSurfaceView 
//   → 设置 OpenGLRenderer 
//   → GLSurfaceView 创建 OpenGL 上下文
//   → 调用 OpenGLRenderer.onSurfaceCreated()
//   → onSurfaceCreated() 调用 nativeInit() (这里！)
//   → nativeInit() 编译 GLSL 着色器并创建缓冲区
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_ndklearn2_OpenGLRenderer_nativeInit(JNIEnv* env, jobject thiz) {
    LOGI("Initializing OpenGL ES 3.0");
    
    // 【步骤 1】编译 GLSL 着色器代码，创建着色器程序
    // 这里会编译上面定义的 vertexShaderSource 和 fragmentShaderSource
    gProgram = createProgram();
    if (gProgram == 0) {
        LOGE("Failed to create shader program");
        return JNI_FALSE;
    }
    
    // 定义三角形的顶点数据（位置 + 颜色）
    // 每个顶点包含：x, y, z, w, r, g, b, a
    float vertices[] = {
        // 位置              // 颜色
        0.0f,  0.5f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f,  // 顶点1 (红色)
       -0.5f, -0.5f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,  // 顶点2 (绿色)
        0.5f, -0.5f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f,  // 顶点3 (蓝色)
    };
    
    // 创建 VAO 和 VBO
    glGenVertexArrays(1, &gVAO);
    glGenBuffers(1, &gVBO);
    
    glBindVertexArray(gVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // 设置顶点属性
    // 位置属性 (location = 0)
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // 颜色属性 (location = 1)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    LOGI("OpenGL initialization successful");
    return JNI_TRUE;
}

// 改变视口大小
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer_nativeResize(JNIEnv* env, jobject thiz, jint width, jint height) {
    LOGI("Resizing viewport to %d x %d", width, height);
    glViewport(0, 0, width, height);
}

// ============================================================
// 【关键函数】渲染一帧 - GLSL 着色器在这里执行！
// ============================================================
// 调用时机：GLSurfaceView 每帧都会调用 onDrawFrame()，然后调用这里
// 
// 完整调用链：
// GLSurfaceView 渲染循环（每帧）
//   → 调用 OpenGLRenderer.onDrawFrame()
//   → onDrawFrame() 调用 nativeRender() (这里！)
//   → nativeRender() 使用编译好的 GLSL 着色器程序绘制
//   → GPU 执行 GLSL 着色器代码
//   → 渲染结果显示在屏幕上
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer_nativeRender(JNIEnv* env, jobject thiz) {
    // 清除颜色缓冲区（设置背景色）
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // 【关键步骤】激活着色器程序
    // 这会让 GPU 使用我们编译好的 GLSL 着色器代码！
    // 之后所有的绘制操作都会使用这个着色器程序
    glUseProgram(gProgram);
    
    // 更新旋转角度（可选：创建旋转效果）
    gRotationAngle += 0.01f;
    const float TWO_PI = 2.0f * 3.14159265358979323846f;
    if (gRotationAngle > TWO_PI) {
        gRotationAngle -= TWO_PI;
    }
    
    // 【关键步骤】绘制三角形
    // 当调用 glDrawArrays() 时：
    // 1. GPU 会执行顶点着色器（vertexShaderSource）处理每个顶点
    // 2. 然后进行光栅化（将三角形转换为像素）
    // 3. GPU 会执行片段着色器（fragmentShaderSource）处理每个像素
    // 4. 最终颜色写入帧缓冲区，显示在屏幕上
    glBindVertexArray(gVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);  // 绘制 3 个顶点组成的三角形
    glBindVertexArray(0);
}

// 清理资源
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_OpenGLRenderer_nativeCleanup(JNIEnv* env, jobject thiz) {
    LOGI("Cleaning up OpenGL resources");
    
    if (gVAO != 0) {
        glDeleteVertexArrays(1, &gVAO);
        gVAO = 0;
    }
    
    if (gVBO != 0) {
        glDeleteBuffers(1, &gVBO);
        gVBO = 0;
    }
    
    if (gProgram != 0) {
        glDeleteProgram(gProgram);
        gProgram = 0;
    }
}


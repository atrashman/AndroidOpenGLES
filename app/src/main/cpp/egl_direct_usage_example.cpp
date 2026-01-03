// ============================================================
// 直接使用 EGL 的示例代码
// ============================================================
// 
// 说明：这个文件展示了如何不通过 GLSurfaceView，直接使用 EGL API
// 来创建 OpenGL 上下文和渲染表面
//
// 注意：这需要从 Java 层传入 ANativeWindow（通过 SurfaceView）
// ============================================================

#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#define LOG_TAG "EGLDirect"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// EGL 相关全局变量
static EGLDisplay gDisplay = EGL_NO_DISPLAY;
static EGLContext gContext = EGL_NO_CONTEXT;
static EGLSurface gSurface = EGL_NO_SURFACE;
static ANativeWindow* gWindow = nullptr;

// OpenGL 渲染相关全局变量
static GLuint gProgram = 0;
static GLuint gVAO = 0;
static GLuint gVBO = 0;
static bool gInitialized = false;

//method
static bool initOpenGLResources();


// ============================================================
// 初始化 EGL 显示
// ============================================================
static bool initEGLDisplay() {
    // 1. 获取默认显示
    // eglGetDisplay(display_id)
    // EGL_DEFAULT_DISPLAY: 使用默认显示（Android 主屏幕）
    gDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (gDisplay == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
        return false;
    }
    
    // 2. 初始化 EGL
    // eglInitialize(display, major, minor)
    // 参数2和3: 返回 EGL 版本号（可以传入 nullptr 忽略）
    EGLint major, minor;
    if (!eglInitialize(gDisplay, &major, &minor)) {
        LOGE("Failed to initialize EGL");
        return false;
    }
    LOGI("EGL initialized: version %d.%d", major, minor);
    
    return true;
}

// ============================================================
// 选择 EGL 配置
// ============================================================
static EGLConfig chooseEGLConfig() {
    // 配置属性：指定我们需要的 OpenGL ES 版本和特性
    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,  // 需要 OpenGL ES 3.0
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,          // 窗口表面
        EGL_BLUE_SIZE, 8,                          // 蓝色通道 8 位
        EGL_GREEN_SIZE, 8,                         // 绿色通道 8 位
        EGL_RED_SIZE, 8,                           // 红色通道 8 位
        EGL_ALPHA_SIZE, 8,                         // Alpha 通道 8 位
        EGL_DEPTH_SIZE, 24,                        // 深度缓冲区 24 位
        EGL_NONE                                  // 结束标记
    };
    
    EGLint numConfigs;
    EGLConfig config;
    
    // eglChooseConfig(display, attribs, configs, config_size, num_configs)
    // 参数3: 存储找到的配置的数组
    // 参数4: 数组大小（这里只找一个）
    // 参数5: 返回找到的配置数量
    if (!eglChooseConfig(gDisplay, attribs, &config, 1, &numConfigs)) {
        LOGE("Failed to choose EGL config");
        return nullptr;
    }
    
    if (numConfigs == 0) {
        LOGE("No matching EGL config found");
        return nullptr;
    }
    
    return config;
}

// ============================================================
// 创建 EGL 上下文
// ============================================================
static bool createEGLContext(EGLConfig config) {
    // 上下文属性：指定 OpenGL ES 版本
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,  // OpenGL ES 3.0
        EGL_NONE
    };
    
    // eglCreateContext(display, config, share_context, attribs)
    // 参数3share_context: 共享上下文（nullptr 表示不共享） 着色器 纹理贴图等可以共享
    // 参数4: 上下文属性数组
    gContext = eglCreateContext(gDisplay, config, nullptr, contextAttribs);
    if (gContext == EGL_NO_CONTEXT) {
        LOGE("Failed to create EGL context");
        return false;
    }
    
    LOGI("EGL context created");
    return true;
}

// ============================================================
// 创建 EGL 表面（从 ANativeWindow）
// ============================================================
static bool createEGLSurface(EGLConfig config, ANativeWindow* window) {
    // eglCreateWindowSurface(display, config, native_window, attribs)
    // 参数3: ANativeWindow（从 Java Surface 获取）
    // 参数4: 表面属性（nullptr 表示使用默认）
    gSurface = eglCreateWindowSurface(gDisplay, config, window, nullptr);
    if (gSurface == EGL_NO_SURFACE) {
        LOGE("Failed to create EGL surface");
        return false;
    }
    
    LOGI("EGL surface created");
    return true;
}

// ============================================================
// 使上下文成为当前上下文
// ============================================================
static bool makeCurrent() {
    // eglMakeCurrent(display, draw_surface, read_surface, context)
    // 参数2和3: 绘制和读取表面（通常相同）
    // 参数4: 要激活的上下文
    if (!eglMakeCurrent(gDisplay, gSurface, gSurface, gContext)) {
        LOGE("Failed to make EGL context current");
        return false;
    }
    
    LOGI("EGL context made current");
    return true;
}

// ============================================================
// JNI 函数：初始化 EGL（从 Java 传入 Surface）
// ============================================================
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_ndklearn2_EGLRenderer_nativeInitEGL(JNIEnv* env, jobject thiz, jobject surface) {
    LOGI("Initializing EGL directly");
    
    // 1. 从 Java Surface 获取 ANativeWindow
    // ANativeWindow_fromSurface: 将 Java Surface 转换为 ANativeWindow
    gWindow = ANativeWindow_fromSurface(env, surface);
    if (gWindow == nullptr) {
        LOGE("Failed to get native window from surface");
        return JNI_FALSE;
    }
    
    // 2. 初始化 EGL 显示
    if (!initEGLDisplay()) {
        ANativeWindow_release(gWindow);
        return JNI_FALSE;
    }
    
    // 3. 选择 EGL 配置
    EGLConfig config = chooseEGLConfig();
    if (config == nullptr) {
        eglTerminate(gDisplay);
        ANativeWindow_release(gWindow);
        return JNI_FALSE;
    }
    
    // 4. 创建 EGL 上下文
    if (!createEGLContext(config)) {
        eglTerminate(gDisplay);
        ANativeWindow_release(gWindow);
        return JNI_FALSE;
    }
    
    // 5. 创建 EGL 表面
    if (!createEGLSurface(config, gWindow)) {
        eglDestroyContext(gDisplay, gContext);
        eglTerminate(gDisplay);
        ANativeWindow_release(gWindow);
        return JNI_FALSE;
    }
    
    // 6. 使上下文成为当前上下文
    if (!makeCurrent()) {
        eglDestroySurface(gDisplay, gSurface);
        eglDestroyContext(gDisplay, gContext);
        eglTerminate(gDisplay);
        ANativeWindow_release(gWindow);
        return JNI_FALSE;
    }
    
    // 7. 获取 OpenGL ES 版本信息
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    LOGI("OpenGL ES Version: %s", version);
    LOGI("OpenGL ES Renderer: %s", renderer);
    
    // 8. 初始化 OpenGL 渲染资源（着色器、VAO/VBO）
    if (!initOpenGLResources()) {
        LOGE("Failed to initialize OpenGL resources");
        eglDestroySurface(gDisplay, gSurface);
        eglDestroyContext(gDisplay, gContext);
        eglTerminate(gDisplay);
        ANativeWindow_release(gWindow);
        return JNI_FALSE;
    }
    
    LOGI("EGL initialization successful");
    gInitialized = true;
    return JNI_TRUE;
}

// ============================================================
// JNI 函数：交换缓冲区（显示渲染结果）
// ============================================================
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_EGLRenderer_nativeSwapBuffers(JNIEnv* env, jobject thiz) {
    // eglSwapBuffers(display, surface)
    // 作用：将后台缓冲区的内容交换到前台，显示在屏幕上
    if (gDisplay != EGL_NO_DISPLAY && gSurface != EGL_NO_SURFACE) {
        eglSwapBuffers(gDisplay, gSurface);
    }
}

// ============================================================
// JNI 函数：清理 EGL 资源
// ============================================================
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_EGLRenderer_nativeCleanupEGL(JNIEnv* env, jobject thiz) {
    LOGI("Cleaning up EGL resources");
    
    // 1. 取消当前上下文
    if (gDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(gDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    
    // 2. 销毁表面
    if (gDisplay != EGL_NO_DISPLAY && gSurface != EGL_NO_SURFACE) {
        eglDestroySurface(gDisplay, gSurface);
        gSurface = EGL_NO_SURFACE;
    }
    
    // 3. 销毁上下文
    if (gDisplay != EGL_NO_DISPLAY && gContext != EGL_NO_CONTEXT) {
        eglDestroyContext(gDisplay, gContext);
        gContext = EGL_NO_CONTEXT;
    }
    
    // 4. 清理 OpenGL 资源
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
    
    // 5. 终止 EGL
    if (gDisplay != EGL_NO_DISPLAY) {
        eglTerminate(gDisplay);
        gDisplay = EGL_NO_DISPLAY;
    }
    
    // 6. 释放窗口
    if (gWindow != nullptr) {
        ANativeWindow_release(gWindow);
        gWindow = nullptr;
    }
    
    gInitialized = false;
    LOGI("EGL cleanup complete");
}

// ============================================================
// JNI 函数：改变表面大小
// ============================================================
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_EGLRenderer_nativeSurfaceChanged(JNIEnv* env, jobject thiz, jint width, jint height) {
    LOGI("Surface changed: %d x %d", width, height);
    
    // 设置视口
    if (gDisplay != EGL_NO_DISPLAY && gContext != EGL_NO_CONTEXT) {
        glViewport(0, 0, width, height);
    }
}

// ============================================================
// GLSL 着色器代码
// ============================================================
static const char* vertexShaderSource = R"(
#version 300 es
    layout(location = 0) in vec4 aPosition;
    layout(location = 1) in vec4 aColor;
    out vec4 vColor;
    void main() {
        gl_Position = aPosition;
        vColor = aColor;
    }
)";

static const char* fragmentShaderSource = R"(
#version 300 es
precision mediump float;
    in vec4 vColor;
    out vec4 fragColor;
    void main() {
        fragColor = vColor;
    }
)";

// ============================================================
// 编译着色器
// ============================================================
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
// 创建着色器程序
// ============================================================
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

// ============================================================
// 初始化 OpenGL 渲染资源
// ============================================================
static bool initOpenGLResources() {
    LOGI("Initializing OpenGL resources");
    
    // 1. 创建着色器程序
    gProgram = createProgram();
    if (gProgram == 0) {
        LOGE("Failed to create shader program");
        return false;
    }
    
    // 2. 定义三角形的顶点数据（位置 + 颜色）
    float vertices[] = {
        // 位置              // 颜色
        0.0f,  0.5f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f,  // 顶点1 (红色)
       -0.5f, -0.5f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,  // 顶点2 (绿色)
        0.5f, -0.5f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f,  // 顶点3 (蓝色)
    };
    
    // 3. 创建 VAO 和 VBO
    glGenVertexArrays(1, &gVAO);
    glGenBuffers(1, &gVBO);
    
    glBindVertexArray(gVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // 4. 设置顶点属性
    // 位置属性 (location = 0)
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // 颜色属性 (location = 1)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    LOGI("OpenGL resources initialized successfully");
    return true;
}

// ============================================================
// JNI 函数：渲染一帧
// ============================================================
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_EGLRenderer_nativeRender(JNIEnv* env, jobject thiz) {
    if (!gInitialized) {
        return;
    }
    
    // 清除颜色缓冲区
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // 使用着色器程序
    glUseProgram(gProgram);
    
    // 绘制三角形
    glBindVertexArray(gVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}


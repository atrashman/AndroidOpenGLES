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
    
    // ============================================================
    // 【第一部分】定义顶点数据
    // ============================================================
    // 
    // 顶点数据格式：每个顶点包含 8 个 float 值
    // [位置 x, y, z, w] + [颜色 r, g, b, a] = 8 个 float
    //
    // 数据布局（内存中的排列）：
    // 顶点1: [x1, y1, z1, w1, r1, g1, b1, a1]
    // 顶点2: [x2, y2, z2, w2, r2, g2, b2, a2]
    // 顶点3: [x3, y3, z3, w3, r3, g3, b3, a3]
    //
    // 坐标系统说明：
    // - OpenGL 使用归一化设备坐标 (NDC)
    // - X 轴：-1.0 (左) 到 1.0 (右)
    // - Y 轴：-1.0 (下) 到 1.0 (上)
    // - Z 轴：-1.0 (远) 到 1.0 (近)
    // - W 值：齐次坐标，通常为 1.0
    // ============================================================
    float vertices[] = {
        // 位置              // 颜色
        0.0f,  0.5f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f,  // 顶点1 (红色)
       -0.5f, -0.5f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,  // 顶点2 (绿色)
        0.5f, -0.5f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f,  // 顶点3 (蓝色)
    };
    // 总共：3 个顶点 × 8 个 float = 24 个 float = 24 × 4 字节 = 96 字节
    
    // ============================================================
    // 【第二部分】创建 VAO 和 VBO
    // ============================================================
    //
    // VAO (Vertex Array Object) - 顶点数组对象
    // 作用：保存顶点属性的配置（就像一个"配置容器"）
    // 包含：哪些属性启用、每个属性的格式、数据来源等
    //
    // VBO (Vertex Buffer Object) - 顶点缓冲区对象
    // 作用：在 GPU 内存中存储顶点数据（实际的数据存储）
    // 好处：数据在 GPU 内存中，访问速度快
    //
    // 关系：
    // VAO ──引用──> VBO ──存储──> 顶点数据
    // (配置)        (存储)        (实际数据)
    // ============================================================
    
    // glGenVertexArrays(数量, 存储ID的数组)
    // 参数1: 1 - 要创建的 VAO 数量
    // 参数2: &gVAO - 存储生成的 VAO ID 的地址
    // 作用：在 GPU 中创建一个 VAO，返回一个 ID（类似文件句柄）
    glGenVertexArrays(1, &gVAO);
    // 结果：gVAO 现在包含一个 VAO 的 ID（例如：1）
    
    // glGenBuffers(数量, 存储ID的数组)
    // 参数1: 1 - 要创建的缓冲区数量
    // 参数2: &gVBO - 存储生成的 VBO ID 的地址
    // 作用：在 GPU 中创建一个缓冲区对象，返回一个 ID
    glGenBuffers(1, &gVBO);
    // 结果：gVBO 现在包含一个 VBO 的 ID（例如：2）
    
    // ============================================================
    // 【第三部分】绑定和上传数据
    // ============================================================
    //
    // 绑定（Bind）的作用：
    // - 告诉 OpenGL "我现在要操作这个对象"
    // - 类似于"选中"一个文件进行操作
    // - 后续的操作会作用在绑定的对象上
    // ============================================================
    
    // glBindVertexArray(VAO_ID)
    // 参数: gVAO - 要绑定的 VAO 的 ID
    // 作用：激活这个 VAO，后续的顶点属性配置会保存到这个 VAO 中
    glBindVertexArray(gVAO);
    // 现在：所有顶点属性配置都会保存到 gVAO 中
    
    // glBindBuffer(目标, 缓冲区ID)
    // 参数1: GL_ARRAY_BUFFER - 缓冲区类型（用于存储顶点属性数据）
    //        其他类型：GL_ELEMENT_ARRAY_BUFFER（索引数据）
    // 参数2: gVBO - 要绑定的 VBO 的 ID
    // 作用：激活这个 VBO，后续的缓冲区操作会作用在这个 VBO 上
    glBindBuffer(GL_ARRAY_BUFFER, gVBO);
    // 现在：所有对 GL_ARRAY_BUFFER 的操作都会作用在 gVBO 上
    
    // glBufferData(目标, 数据大小, 数据指针, 使用方式)
    // 参数1: GL_ARRAY_BUFFER - 缓冲区类型（必须和 glBindBuffer 一致）
    // 参数2: sizeof(vertices) - 数据大小（字节数）
    //        = 24 个 float × 4 字节/float = 96 字节
    // 参数3: vertices - 指向 CPU 内存中数据的指针
    // 参数4: GL_STATIC_DRAW - 数据使用方式
    //        GL_STATIC_DRAW: 数据不会改变，用于多次绘制（推荐）
    //        GL_DYNAMIC_DRAW: 数据会频繁改变
    //        GL_STREAM_DRAW: 数据每次绘制都改变
    // 作用：将 CPU 内存中的顶点数据复制到 GPU 内存（VBO）中
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    // 结果：顶点数据现在存储在 GPU 的 gVBO 中
    
    // ============================================================
    // 【第四部分】设置顶点属性（告诉 GPU 如何读取数据）
    // ============================================================
    //
    // 顶点属性（Vertex Attribute）：
    // - 每个顶点可以有多个属性：位置、颜色、纹理坐标、法线等
    // - 需要告诉 GPU：每个属性在数据中的位置、格式、如何读取
    //
    // 数据布局回顾：
    // 每个顶点：8 个 float = [x, y, z, w, r, g, b, a]
    //          位置(4个)    颜色(4个)
    //          偏移0        偏移16字节
    // ============================================================
    
    // glVertexAttribPointer(属性位置, 组件数量, 数据类型, 是否归一化, 步长, 偏移量)
    // 
    // 参数1: 0 - 属性位置（location）
    //        对应着色器中的：layout(location = 0) in vec4 aPosition;
    //        必须和着色器中的 location 一致！
    //
    // 参数2: 4 - 每个属性的组件数量
    //        vec4 有 4 个组件（x, y, z, w）
    //        vec3 有 3 个组件，vec2 有 2 个组件
    //
    // 参数3: GL_FLOAT - 数据类型
    //        每个组件是 float 类型（4 字节）
    //        其他类型：GL_INT, GL_UNSIGNED_BYTE 等
    //
    // 参数4: GL_FALSE - 是否归一化
    //        GL_TRUE: 将整数类型归一化到 [0,1] 或 [-1,1]
    //        GL_FALSE: 不归一化，直接使用原始值
    //        对于 float 类型，通常设为 GL_FALSE
    //
    // 参数5: 8 * sizeof(float) - 步长（Stride）
    //        = 8 × 4 = 32 字节
    //        含义：从一个顶点的这个属性到下一个顶点的这个属性，需要跳过多少字节
    //        因为每个顶点有 8 个 float，所以步长是 8 × 4 = 32 字节
    //
    // 参数6: (void*)0 - 偏移量（Offset）
    //        这个属性在顶点数据中的起始位置
    //        位置属性在开头，所以偏移是 0
    //        (void*)0 表示从数据开始处读取
    //
    // 作用：告诉 GPU 如何从 VBO 中读取位置属性
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    
    // glEnableVertexAttribArray(属性位置)
    // 参数: 0 - 属性位置（必须和 glVertexAttribPointer 的第一个参数一致）
    // 作用：启用这个顶点属性
    //       如果不启用，GPU 不会读取这个属性的数据
    glEnableVertexAttribArray(0);
    // 现在：位置属性（location = 0）已配置并启用
    
    // 颜色属性（location = 1）
    // glVertexAttribPointer(属性位置, 组件数量, 数据类型, 是否归一化, 步长, 偏移量)
    //
    // 参数1: 1 - 属性位置
    //        对应着色器中的：layout(location = 1) in vec4 aColor;
    //
    // 参数2: 4 - vec4 有 4 个组件（r, g, b, a）
    //
    // 参数3: GL_FLOAT - float 类型
    //
    // 参数4: GL_FALSE - 不归一化
    //
    // 参数5: 8 * sizeof(float) - 步长（和位置属性一样）
    //        因为数据布局相同
    //
    // 参数6: (void*)(4 * sizeof(float)) - 偏移量
    //        = (void*)(4 × 4) = (void*)16
    //        含义：颜色属性在顶点数据中的起始位置
    //        每个顶点：[x, y, z, w, r, g, b, a]
    //                 位置(4个float) 颜色(4个float)
    //                 0-15字节        16-31字节
    //        所以颜色从第 16 字节开始（跳过前 4 个 float）
    //
    // 作用：告诉 GPU 如何从 VBO 中读取颜色属性
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    
    // 启用颜色属性
    glEnableVertexAttribArray(1);
    // 现在：颜色属性（location = 1）已配置并启用
    
    // ============================================================
    // 【第五部分】解绑（可选，但推荐）
    // ============================================================
    //
    // 解绑的作用：
    // - 防止意外修改已配置好的对象
    // - 类似于"保存并关闭文件"
    // - 不是必须的，但是良好的编程习惯
    // ============================================================
    
    // glBindBuffer(目标, 0)
    // 参数1: GL_ARRAY_BUFFER - 缓冲区类型
    // 参数2: 0 - 解绑（0 表示"不绑定任何对象"）
    // 作用：取消 VBO 的绑定
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // 现在：不再绑定任何 VBO，防止意外修改
    
    // glBindVertexArray(0)
    // 参数: 0 - 解绑
    // 作用：取消 VAO 的绑定
    glBindVertexArray(0);
    // 现在：不再绑定任何 VAO
    // 注意：VAO 中已经保存了所有配置，解绑不影响已保存的配置
    
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


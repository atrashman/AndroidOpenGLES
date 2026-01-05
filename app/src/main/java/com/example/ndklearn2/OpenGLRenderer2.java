package com.example.ndklearn2;

import android.content.Context;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
/**
 * OpenGL ES 渲染器
 *
 * 【GLSurfaceView 和 GLSL 的关联】
 *
 * 这个类实现了 GLSurfaceView.Renderer 接口，是连接 GLSurfaceView 和 GLSL 着色器的桥梁：
 *
 * 调用流程：
 * 1. MainActivity 创建 GLSurfaceView 并设置这个 Renderer
 * 2. GLSurfaceView 创建 OpenGL 上下文后，调用 onSurfaceCreated()
 *    → 这里调用 nativeInit()，在 C++ 中编译 GLSL 着色器代码
 * 3. GLSurfaceView 每帧调用 onDrawFrame()
 *    → 这里调用 nativeRender()，在 C++ 中使用 GLSL 着色器绘制
 * 4. GLSL 着色器在 GPU 上执行，渲染结果显示在屏幕上
 *
 * 关键点：
 * - GLSurfaceView 负责创建 OpenGL 上下文和渲染循环
 * - GLSL 着色器代码在 C++ 中定义和编译
 * - 渲染时 GPU 执行 GLSL 代码，决定最终显示效果
 */
public class OpenGLRenderer2 implements GLSurfaceView.Renderer {

    private Context mContext;
    public OpenGLRenderer2(Context context){
        mContext= context;
    }

    static {
        System.loadLibrary("ndklearn2");
    }

    // Native 方法声明 - 这些方法在 C++ 中实现，会编译和执行 GLSL 着色器

    /**
     * 初始化 OpenGL 和编译 GLSL 着色器
     * 对应 C++ 函数：Java_com_example_ndklearn2_OpenGLRenderer_nativeInit
     * 在 opengl_renderer.cpp 中会编译 vertexShaderSource 和 fragmentShaderSource
     */
    private native boolean nativeInit();

    /**
     * 改变视口大小
     */
    private native void nativeResize(int width, int height);

    /**
     * 渲染一帧 - GLSL 着色器在这里执行！
     * 对应 C++ 函数：Java_com_example_ndklearn2_OpenGLRenderer_nativeRender
     * 在 opengl_renderer.cpp 中会调用 glUseProgram() 和 glDrawArrays()
     * GPU 会执行编译好的 GLSL 着色器代码
     */
    private native void nativeRender();

    /**
     * 清理资源
     */
    private native void nativeCleanup();

    /**
     * 【步骤 1】OpenGL 上下文创建时调用
     * 时机：GLSurfaceView 创建 OpenGL 上下文后，只调用一次
     * 作用：初始化 OpenGL，编译 GLSL 着色器代码
     */
    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        // 调用 C++ 函数，编译 GLSL 着色器
        nativeInit();
        loadTexture(R.drawable.texture);
        loadUniform();
        loadVertice();
    }

    private native void loadUniform();


    private native void loadVertice();

    public void loadTexture(int resourceID){
        // 1. 从资源中解码 Bitmap (设置为不缩放，确保原始大小)
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        Bitmap bitmap = BitmapFactory.decodeResource(mContext.getResources(), resourceID, options);
        // 2. 绑定这个 Bitmap 到 OpenGL (假设你已经绑定了 texture ID)
        loadTextureFromBitmap(bitmap);
        // 3. 及时回收 Bitmap (可选，但这还是由 Java GC 管理)
        bitmap.recycle();
    }
    public native void loadTextureFromBitmap(Bitmap bitmap);
    /**
     * 【步骤 2】表面大小改变时调用
     * 时机：GLSurfaceView 大小改变时（如旋转屏幕）
     */
    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        nativeResize(width, height);
    }

    /**
     * 【步骤 3】每一帧绘制时调用 - 这是渲染循环的核心！
     * 时机：GLSurfaceView 每帧都会调用（如果设置为 RENDERMODE_CONTINUOUSLY）
     * 作用：使用编译好的 GLSL 着色器程序进行绘制
     *
     * 这里会调用 nativeRender()，在 C++ 中：
     * 1. 调用 glUseProgram(gProgram) - 激活 GLSL 着色器程序
     * 2. 调用 glDrawArrays() - 触发绘制
     * 3. GPU 执行顶点着色器和片段着色器（GLSL 代码）
     * 4. 渲染结果显示在屏幕上
     */
    @Override
    public void onDrawFrame(GL10 gl) {
        // 调用 C++ 函数，使用 GLSL 着色器绘制
        nativeRender();
    }

    /**
     * 清理资源（在 Activity 销毁时调用）
     */
    public void cleanup() {
        nativeCleanup();
    }
}


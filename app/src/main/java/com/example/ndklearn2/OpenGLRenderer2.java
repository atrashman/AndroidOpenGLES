package com.example.ndklearn2;

import android.content.Context;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.Matrix;
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
    
    // 变换矩阵
    private float[] modelMatrix = new float[16];
    private float[] viewMatrix = new float[16];
    private float[] projectionMatrix = new float[16];
    private float[] normalMatrix = new float[9];
    
    // 相机位置
    private float[] cameraPos = new float[3];
    
    // 光照参数
    private float[] ambientColor = new float[]{0.2f, 0.2f, 0.2f};
    private float[] diffuseColor = new float[]{1.0f, 1.0f, 1.0f};
    private float[] specularColor = new float[]{1.0f, 1.0f, 1.0f};
    private float[] lightDirection = new float[]{0.0f, 0.0f, 0.0f}; // 0,0,0表示点光源
    private float[] lightPos = new float[]{2.0f, 2.0f, 2.0f};
    private float[] attenuationFactors = new float[]{1.0f, 0.09f, 0.032f};
    private float spotExponent = 0.0f;
    private float spotCutoffAngle = 0.0f;
    private float[] spotDirection = new float[]{0.0f, 0.0f, 0.0f};
    private int computeDistanceAttenuation = 1;
    
    // 材质参数
    private float[] materialAmbient = new float[]{0.2f, 0.2f, 0.2f};
    private float[] materialDiffuse = new float[]{0.8f, 0.8f, 0.8f};
    private float[] materialSpecular = new float[]{1.0f, 1.0f, 1.0f};
    private float materialShininess = 32.0f;
    
    public OpenGLRenderer2(Context context){
        mContext= context;
        initMatrices();
    }
    
    private void initMatrices() {
        // 模型矩阵：单位矩阵（不进行变换）世界坐标即物体坐标
        Matrix.setIdentityM(modelMatrix, 0);
        
        // 相机位置：在 (2.5, 2.5, 2.5) 位置，看向原点，可以看到三个面（正X、正Y、正Z）
        // 保持合适的距离，既能看清三个面，又不会太远
        cameraPos[0] = 2.5f;
        cameraPos[1] = 2.5f;
        cameraPos[2] = 2.5f;
        
        // 视图矩阵：从相机位置看向原点，上方向为Y轴正方向
        float eyeX = cameraPos[0];
        float eyeY = cameraPos[1];
        float eyeZ = cameraPos[2];
        float centerX = 0.0f;
        float centerY = 0.0f;
        float centerZ = 0.0f;
        float upX = 0.0f;
        float upY = 1.0f;
        float upZ = 0.0f;
        //上方向 (0, 1, 0)
        Matrix.setLookAtM(viewMatrix, 0, eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ);
        
        // 投影矩阵：透视投影（在onSurfaceChanged中会根据屏幕尺寸更新）
        // 使用 FOV（视场角）方式，更直观
        // FOV 越小，物体显示越大（类似长焦镜头）
        float fovy = 45.0f;  // 垂直视场角（度），值越小物体越大
        float aspect = 1.0f; // 宽高比（在onSurfaceChanged中会更新）
        float near = 1.0f;   // 近裁剪平面距离
        float far = 100.0f;  // 远裁剪平面距离
        Matrix.perspectiveM(projectionMatrix, 0, fovy, aspect, near, far);
        
        // 法线矩阵：从模型矩阵提取3x3部分并求逆转置
        Matrix.invertM(normalMatrix, 0, modelMatrix, 0);
        Matrix.transposeM(normalMatrix, 0, normalMatrix, 0);
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

        


        updateTransformUBO(modelMatrix, viewMatrix, projectionMatrix, normalMatrix);
        updateLightUBO(ambientColor, diffuseColor, specularColor, lightDirection, lightPos, attenuationFactors, spotExponent, spotCutoffAngle, spotDirection, computeDistanceAttenuation);
        updateMaterialUBO(materialAmbient, materialDiffuse, materialSpecular, materialShininess);
        updateCameraPos(cameraPos);
    }

    
    private native void updateTransformUBO(float[] modelMatrix, float[] viewMatrix, float[] projectionMatrix, float[] normalMatrix);
    private native void updateLightUBO(float[] ambientColor, float[] diffuseColor, float[] specularColor, float[] lightDirection, float[] lightPos, float[] attenuationFactors, float spotExponent, float spotCutoffAngle, float[] spotDirection, int computeDistanceAttenuation);
    private native void updateMaterialUBO(float[] materialAmbient, float[] materialDiffuse, float[] materialSpecular, float materialShininess);
    private native void updateCameraPos(float[] cameraPos);


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
        
        // 更新投影矩阵以适应新的屏幕尺寸
        // 使用 FOV（视场角）方式，更直观
        float fovy = 45.0f;  // 垂直视场角（度），值越小物体越大
        float aspect = (float) width / height;  // 宽高比
        float near = 1.0f;   // 近裁剪平面距离
        float far = 100.0f;  // 远裁剪平面距离
        Matrix.perspectiveM(projectionMatrix, 0, fovy, aspect, near, far);
        
        // 更新变换矩阵UBO
        updateTransformUBO(modelMatrix, viewMatrix, projectionMatrix, normalMatrix);
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


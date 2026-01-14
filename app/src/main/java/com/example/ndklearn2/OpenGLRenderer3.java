package com.example.ndklearn2;

import android.content.Context;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
/**
 * OpenGL ES 渲染器 3 - 使用共享工具库的简化版本
 */
public class OpenGLRenderer3 implements GLSurfaceView.Renderer {

    private Context mContext;
    private long lastTime;


    public OpenGLRenderer3(Context context) {
        mContext = context;
    }

    static {
        System.loadLibrary("ndklearn2");
    }

    // Native 方法声明
    private native boolean nativeInit();
    private native void nativeResize(int width, int height);
    private native void nativeRender();
    private native void nativeCleanup();

    private native void loadTextureFromBitmap(Bitmap bitmap);
    private native void releaseTexture();
    private native void initTFBBuffer();
    private native void initVAO();
    private native void initUBO() ;

    /**
     * OpenGL 上下文创建时调用
     */
    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        if (!nativeInit()) {
            android.util.Log.e("OpenGLRenderer3", "Failed to initialize renderer");
            return;
        }
        // 初始化TFB缓冲区（存储粒子属性）用于代替VBO
        initTFBBuffer();

        // 初始化VAO（绑定顶点属性）
        initVAO();

        // 初始化UBO
        initUBO();

        // 记录初始时间
        lastTime = System.currentTimeMillis();
    }



    /**
     * 加载纹理资源
     */
    public void loadTexture(int resourceID) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        Bitmap bitmap = BitmapFactory.decodeResource(mContext.getResources(), resourceID, options);
        loadTextureFromBitmap(bitmap);
        bitmap.recycle();
    }

    /**
     * 表面大小改变时调用
     */
    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        nativeResize(width, height);
    }

    /**
     * 每一帧绘制时调用
     */
    @Override
    public void onDrawFrame(GL10 gl) {
        nativeRender();
    }

    /**
     * 清理资源
     */
    public void cleanup() {
        nativeCleanup();
    }
    
}


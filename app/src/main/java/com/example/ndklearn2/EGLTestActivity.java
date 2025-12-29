package com.example.ndklearn2;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.view.SurfaceView;

/**
 * EGL 直接使用测试 Activity
 * 
 * 这个 Activity 展示了如何不通过 GLSurfaceView，直接使用 EGL API
 * 来创建 OpenGL 上下文和渲染表面，绘制三角形
 */
public class EGLTestActivity extends AppCompatActivity {

    // Used to load the 'ndklearn2' library on application startup.
    static {
        System.loadLibrary("ndklearn2");
    }

    private SurfaceView surfaceView;
    private EGLRenderer eglRenderer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // 创建 SurfaceView（用于显示 OpenGL 内容）
        surfaceView = new SurfaceView(this);
        
        // 创建 EGL 渲染器
        eglRenderer = new EGLRenderer();
        
        // 初始化 EGL 渲染器（会自动监听 Surface 的创建）
        eglRenderer.init(surfaceView);
        
        // 设置为内容视图
        setContentView(surfaceView);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (eglRenderer != null) {
            eglRenderer.cleanup();
        }
    }
}


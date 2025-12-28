package com.example.ndklearn2;

import androidx.appcompat.app.AppCompatActivity;

import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.FrameLayout;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'ndklearn2' library on application startup.
    static {
        System.loadLibrary("ndklearn2");
    }

    private GLSurfaceView glSurfaceView;
    private OpenGLRenderer renderer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // 创建 GLSurfaceView
        glSurfaceView = new GLSurfaceView(this);
        
        // 设置 OpenGL ES 版本为 3.0
        glSurfaceView.setEGLContextClientVersion(3);
        
        // 设置渲染器
        renderer = new OpenGLRenderer();
        glSurfaceView.setRenderer(renderer);
        
        // 设置为持续渲染模式（可选：RENDERMODE_WHEN_DIRTY 为按需渲染）
        glSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        
        // 将 GLSurfaceView 设置为内容视图
        setContentView(glSurfaceView);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (glSurfaceView != null) {
            glSurfaceView.onResume();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (glSurfaceView != null) {
            glSurfaceView.onPause();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (renderer != null) {
            renderer.cleanup();
        }
    }

    /**
     * A native method that is implemented by the 'ndklearn2' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();

    public native String intArray2String(int[] arr);
}
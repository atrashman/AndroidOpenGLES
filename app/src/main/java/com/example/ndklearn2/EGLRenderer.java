package com.example.ndklearn2;

import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * 直接使用 EGL 的渲染器示例
 * 
 * 这个类展示了如何不通过 GLSurfaceView，直接使用 EGL API
 * 来创建 OpenGL 上下文和渲染表面
 * 
 * 关键区别：
 * - GLSurfaceView: 自动管理 EGL，提供渲染循环
 * - 直接使用 EGL: 手动管理 EGL，需要自己实现渲染循环
 */
public class EGLRenderer {
    
    static {
        System.loadLibrary("ndklearn2");
    }
    
    private SurfaceView surfaceView;
    private RenderThread renderThread;
    
    // Native 方法声明
    private native boolean nativeInitEGL(Surface surface);
    private native void nativeRender();
    private native void nativeSwapBuffers();
    private native void nativeCleanupEGL();
    private native void nativeSurfaceChanged(int width, int height);
    
    /**
     * 初始化 EGL 渲染器
     * @param surfaceView 用于显示的 SurfaceView
     */
    public void init(SurfaceView surfaceView) {
        this.surfaceView = surfaceView;
        
        // 监听 Surface 创建和销毁
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                // Surface 创建时，初始化 EGL
                Surface surface = holder.getSurface();
                if (nativeInitEGL(surface)) {
                    // 启动渲染线程
                    startRenderThread();
                }
            }
            
            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                // Surface 大小改变时
                nativeSurfaceChanged(width, height);
            }
            
            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                // Surface 销毁时，停止渲染并清理
                stopRenderThread();
                nativeCleanupEGL();
            }
        });
    }
    
    /**
     * 启动渲染线程
     */
    private void startRenderThread() {
        if (renderThread != null) {
            return;
        }
        
        renderThread = new RenderThread();
        renderThread.start();
    }
    
    /**
     * 停止渲染线程
     */
    private void stopRenderThread() {
        if (renderThread != null) {
            renderThread.stopRendering();
            try {
                renderThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            renderThread = null;
        }
    }
    
    /**
     * 渲染线程（手动实现的渲染循环）
     */
    private class RenderThread extends Thread {
        private boolean running = true;
        
        @Override
        public void run() {
            while (running) {
                // 调用渲染函数
                nativeRender();
                
                // 交换缓冲区，显示渲染结果
                nativeSwapBuffers();
                
                // 控制帧率（可选）
                try {
                    Thread.sleep(16);  // 约 60 FPS
                } catch (InterruptedException e) {
                    break;
                }
            }
        }
        
        public void stopRendering() {
            running = false;
            interrupt();
        }
    }
    
    /**
     * 清理资源
     */
    public void cleanup() {
        stopRenderThread();
        nativeCleanupEGL();
    }
}


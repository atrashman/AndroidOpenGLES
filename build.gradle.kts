// Top-level build file where you can add configuration options common to all sub-projects/modules.
plugins {
    id("java-library")
    id("application")  // 添加 application 插件
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

// 配置主类
application {
    mainClass.set("com.example.lib.MyClass")
}

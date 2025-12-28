plugins {
    id("java-library")
    id("application")
}

java {
    sourceCompatibility = JavaVersion.VERSION_11
    targetCompatibility = JavaVersion.VERSION_11
}

application {
    mainClass.set("com.example.lib.MyClass")
}

// 添加一个自定义任务来运行指定的主类
tasks.register<JavaExec>("MyClassMain") {
    group = "application"
    description = "Runs the main method of MyClass"
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("com.example.lib.MyClass")
}
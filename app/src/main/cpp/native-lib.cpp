#include <jni.h>
#include <string>
#include <android/log.h>

#define LOG_TAG "YourTag"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


//https://blog.csdn.net/jianwei_zhou/article/details/86611928 JNI基本 异常处理 JNI中的引用

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ndklearn2_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    LOGD("Message: %s", hello.c_str());
    return env->NewStringUTF(hello.c_str());
}

// 将 int 数组转换为字符串
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_ndklearn2_MainActivity_intArray2String(
        JNIEnv* env,
        jobject /* this */,
        jintArray arr) {
    
    // 1. 检查数组是否为空
    if (arr == nullptr) {
        LOGD("Array is null");
        return env->NewStringUTF("null");
    }
    
    // 2. 获取数组长度
    jint length = env->GetArrayLength(arr);
    LOGD("Array length: %d", length);
    
    // 3. 获取数组元素
    jboolean isCopy;
    jint* elements = env->GetIntArrayElements(arr, &isCopy);
    if (elements == nullptr) {
        LOGE("Failed to get array elements");
        return env->NewStringUTF("error");
    }
    
    // 4. 构建字符串
    std::string result = "[";
    for (int i = 0; i < length; i++) {
        result += std::to_string(elements[i]);
        if (i < length - 1) {
            result += ", ";
        }
    }
    result += "]";
    
    // 5. 释放数组元素
    env->ReleaseIntArrayElements(arr, elements, JNI_ABORT);
    
    // 6. 返回字符串
    LOGD("Converted array to string: %s", result.c_str());
    return env->NewStringUTF(result.c_str());
}

// 示例1：创建并返回一个 int 数组
extern "C" JNIEXPORT jintArray JNICALL
Java_com_example_ndklearn2_MainActivity_createIntArray(
        JNIEnv* env,
        jobject /* this */,
        jint size) {
    
    LOGD("Creating int array with size: %d", size);
    
    // 1. 创建 jintArray
    jintArray result = env->NewIntArray(size);
    if (result == nullptr) {
        LOGE("Failed to create int array");
        return nullptr; // 内存分配失败
    }
    
    // 2. 创建临时的 C++ 数组并填充数据
    jint* temp = new jint[size];
    for (int i = 0; i < size; i++) {
        temp[i] = i * 10; // 填充示例数据：0, 10, 20, 30...
    }
    
    // 3. 将 C++ 数组的数据复制到 jintArray
    env->SetIntArrayRegion(result, 0, size, temp);
    
    // 4. 清理临时数组
    delete[] temp;
    
    LOGD("Int array created successfully");
    return result;
}

// 示例2：接收一个 int 数组，计算总和并返回
extern "C" JNIEXPORT jint JNICALL
Java_com_example_ndklearn2_MainActivity_sumIntArray(
        JNIEnv* env,
        jobject /* this */,
        jintArray array) {
    
    // 1. 获取数组长度
    jint length = env->GetArrayLength(array);
    LOGD("Summing int array with length: %d", length);
    
    // 2. 获取数组元素的指针（方法一：GetIntArrayElements）
    jboolean isCopy;
    jint* elements = env->GetIntArrayElements(array, &isCopy);
    if (elements == nullptr) {
        LOGE("Failed to get array elements");
        return 0;
    }
    
    // 3. 计算总和
    jint sum = 0;
    for (int i = 0; i < length; i++) {
        sum += elements[i];
    }
    
    // 4. 释放数组元素（重要！防止内存泄漏）
    // JNI_ABORT: 不将修改写回原数组
    // 0: 将修改写回原数组并释放
    env->ReleaseIntArrayElements(array, elements, JNI_ABORT);
    
    LOGD("Sum calculated: %d", sum);
    return sum;
}

// 示例3：修改传入的数组（每个元素乘以2）
extern "C" JNIEXPORT void JNICALL
Java_com_example_ndklearn2_MainActivity_doubleIntArray(
        JNIEnv* env,
        jobject /* this */,
        jintArray array) {
    
    LOGD("Doubling int array elements");
    
    // 1. 获取数组长度
    jint length = env->GetArrayLength(array);
    
    // 2. 方法二：使用 GetIntArrayRegion 读取数据到临时数组
    jint* temp = new jint[length];
    env->GetIntArrayRegion(array, 0, length, temp);
    
    // 3. 修改数据
    for (int i = 0; i < length; i++) {
        temp[i] *= 2;
    }
    
    // 4. 将修改后的数据写回原数组
    env->SetIntArrayRegion(array, 0, length, temp);
    
    // 5. 清理临时数组
    delete[] temp;
    
    LOGD("Array elements doubled");
}

// 示例4：创建二维数组
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_example_ndklearn2_MainActivity_create2DIntArray(
        JNIEnv* env,
        jobject /* this */,
        jint rows,
        jint cols) {
    
    LOGD("Creating 2D int array with rows: %d, cols: %d", rows, cols);
    
    // 1. 获取 int[] 的类
    jclass intArrayClass = env->FindClass("[I");
    
    // 2. 创建 int[][] 数组（对象数组）
    jobjectArray result = env->NewObjectArray(rows, intArrayClass, nullptr);
    
    // 3. 为每一行创建一个 int[] 数组
    for (int i = 0; i < rows; i++) {
        jintArray row = env->NewIntArray(cols);
        
        // 填充数据
        jint* temp = new jint[cols];
        for (int j = 0; j < cols; j++) {
            temp[j] = i * cols + j; // 示例数据
        }
        env->SetIntArrayRegion(row, 0, cols, temp);
        delete[] temp;
        
        // 将这一行设置到二维数组中
        env->SetObjectArrayElement(result, i, row);
        
        // 删除局部引用
        env->DeleteLocalRef(row);
    }
    
    LOGD("2D int array created successfully");
    return result;
}

//调用对象的方法 带返回值
extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_ndklearn2_model_Dog_getName(JNIEnv *env, jobject thiz) {
    // TODO: implement getName()
    LOGD("Getting dog name");
    //获取类
    jclass clzz = env->GetObjectClass(thiz);
    //获取属性
    jfieldID mID = env->GetFieldID(clzz, "name", "Ljava/lang/String;");
    return static_cast<jstring>(env->GetObjectField(thiz, mID));
}

//调用对象方法 返回基本类型
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ndklearn2_model_Dog_getAge(JNIEnv *env, jobject thiz) {
    // TODO: implement getAge()
    LOGD("Getting dog age");
    //获取类
    jclass clzz = env->GetObjectClass(thiz);
    //获取属性
    jfieldID mID = env->GetFieldID(clzz, "age", "I");
    return static_cast<jint>(env->GetIntField(thiz, mID));
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_ndklearn2_model_Dog_fight(JNIEnv *env, jobject thiz, jobject dog) {
    // TODO: implement fight()
    LOGD("Dog fight initiated");
    //获取类
    jclass clzz = env->GetObjectClass(thiz);
    //获取属性
    jfieldID mIDAge = env->GetFieldID(clzz, "age", "I");
    //获取属性
    jfieldID mIDName = env->GetFieldID(clzz, "name", "Ljava/lang/String;");

    auto aage = static_cast<jint>(env->GetIntField(thiz, mIDAge));
    auto aname = static_cast<jstring>(env->GetObjectField(thiz, mIDName));
    auto bage = static_cast<jint>(env->GetIntField(dog, mIDAge));
    auto bname = static_cast<jstring>(env->GetObjectField(dog, mIDName));
    jboolean isCopy = false;
    std::string  aname_str = env->GetStringUTFChars(aname, &isCopy);
    std::string  bname_str = env->GetStringUTFChars(bname, &isCopy);
    jboolean result = aage > bage || aname_str.compare(bname_str) >= 0;
    LOGD("Fight result: %s", result ? "true" : "false");
    return result;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_ndklearn2_model_Dog_born(JNIEnv *env, jobject thiz, jstring name, jint age) {
    // TODO: implement born()
    LOGD("Dog born with name and age");
    jclass clzz = env->GetObjectClass(thiz);
    jmethodID mInitID = env->GetMethodID(clzz, "<init>", "(Ljava/lang/String;I)V");
    //创建
    return env->NewObject(clzz, mInitID, name, age);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_ndklearn2_model_Dog_getInstance(JNIEnv *env, jclass clazz, jstring name,
                                                 jint age) {
    // TODO: implement getInstance()
    LOGD("Getting dog instance");
    //sig - 方法签名（字符串） 描述方法的参数和返回值类型 "(Ljava/lang/String;I)V": 签名表示接受一个 String 和一个 int 参数，无返回值
    jmethodID mInitID = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;I)V");
    //创建
    return env->NewObject(clazz, mInitID, name, age);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_lib_Dog_getInstance(JNIEnv *env, jclass clazz, jstring name, jint age) {
    // TODO: implement getInstance()
    LOGD("Getting lib Dog instance");
    jmethodID mInitID = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;I)V");
    //创建
    return env->NewObject(clazz, mInitID, name, age);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_example_lib_MyClass_getMyClass(JNIEnv *env, jclass clazz) {
    // TODO: implement getMyClass()
    LOGD("Getting MyClass instance");
    return env->NewObject(clazz, env->GetMethodID(clazz, "<init>", "()V"));
}

class t {

};
class tstring {
public :
    t& operator[](size_t pos){
        return ts[pos];
    }
private :
    t* ts;
};


void test (){
    tstring str;
    t& rs= str[10];//不调用构造函数
    t c = str[10];//调用拷贝构造
}


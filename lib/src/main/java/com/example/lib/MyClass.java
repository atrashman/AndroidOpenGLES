package com.example.lib;

public class MyClass {

    static{
        System.load("D:/dev/workspace/NdkLearn2/app/build/intermediates/cmake/debug/obj/x86_64/libndklearn2.so");    }
    public static native MyClass getMyClass();

    public static void main(String[] args) {
        //调用native的方法
        MyClass myClass = MyClass.getMyClass();
        //Native method not available on this platform: 'com.example.lib.MyClass com.example.lib.MyClass.getMyClass()'
    }
}
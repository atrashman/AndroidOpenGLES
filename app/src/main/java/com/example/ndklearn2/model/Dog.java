package com.example.ndklearn2.model;

public class Dog {

    String name;
    int age;
    public Dog() {

    }
    public Dog(String name, int age) {
        this.name = name;
        this.age = age;
    }

    public String bark() {
        System.out.println("Dog is barking");
        return "Dog is barking";
    }

    public void eat() {
        System.out.println("Dog is eating");
    }
    public static void sleep() {
        System.out.println("Dog is sleeping");
    }

    public native String getName();

    public native int getAge();

    public native boolean fight(Dog dog);

    public native Dog born(String name, int age);

    public static native Dog getInstance(String name, int age);
}

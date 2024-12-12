/**
 *   Demonstrates use of SFINAE to implement a function that handles 
 * generic non pointer types, generic shared pointer types, and shared
 * pointer types that inherit from specific subclasses
 */

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

class Base_A {};
class Base_B {};
class Base_C {};
template <typename T, typename Enable=void>
struct MyPrint;

class Printer {
public:

    template <typename T>
    static void print() {
        // we wrap print in a struct to allow partial specializations.
        MyPrint<T>::print();
    }

private:

};

// Generic non shared ptr printer
template <typename T, typename Enable>
struct MyPrint {
    static void print() {
        std::cout << "(Regular print)Printer::print<T>: " << T::getName() << std::endl;
    }
};

// Generic partially specialized shared ptr printer
template <typename T, typename Enable>
struct MyPrint<std::shared_ptr<T>, Enable> {
    static void print() {
        std::cout << "(Non specialized)Printer::print<std::shared_ptr<T>>: " << T::getName() << std::endl;
    }
};

// Base A specialized shared ptr printer. Will be preferred by the compiler
// for being more specialized than the generic partial specialization for 
// shared pointers
template <typename T>
struct MyPrint<std::shared_ptr<T>, typename std::enable_if<std::is_base_of<Base_A, T>::value>::type> {
    static void print() {
        std::cout << "(Base_A specialized)Printer::print<std::shared_ptr<T>>: " << T::getName() << std::endl;
    }
};

//
// NOTE: no specialization for Base B, so generic shared pointer version should be called
//

// Base C specialized shared ptr printer. Will be preferred by the compiler 
// for being more specialized than the generic partial specialization for shared
// pointers
template <typename T>
struct MyPrint<std::shared_ptr<T>, typename std::enable_if<std::is_base_of<Base_C, T>::value>::type> {
    static void print() {
        std::cout << "(Base_C specialized)Printer::print<std::shared_ptr<T>>: " << T::getName() << std::endl;
    }
};

class B: public Base_A {
public:
    static std::string getName() { return "B"; }
};

class C {
public:
    static std::string getName() { return "C"; }
};

class D: public Base_B {
public:
    static std::string getName() { return "D"; }
};

class E: public Base_C {
public:
    static std::string getName() { return "E"; }
};

int main() {
    // regular old template function call
    Printer::print<B>(); 

    // regular old template function call
    Printer::print<C>(); 

    // regular old template function call
    Printer::print<D>();

    //regular old template function call
    Printer::print<E>();

    // specialization for shared_ptr Base_A subclasses
    Printer::print<std::shared_ptr<B>>(); 

    // unspecialized shared ptr print
    Printer::print<std::shared_ptr<C>>(); 

    // unspecialized shared ptr print
    Printer::print<std::shared_ptr<D>>();

    // specialization for shared ptr Base_C subclasses
    Printer::print<std::shared_ptr<E>>();

    return 0;
}

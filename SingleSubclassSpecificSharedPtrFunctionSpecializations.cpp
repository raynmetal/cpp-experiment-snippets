/**
 *   Demonstrates use of SFINAE to implement a function that handles 
 * generic non pointer types, shared pointer types to subclasses
 * that inherit from a specific base class, and shared pointer types
 * that don't inherit from that base class.
 * 
 *   The specializations here must be mutually exclusive, and require
 * modifications to the "generic" shared pointer specialization to accommodate
 * new specializations
 */

#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

class Base {};

class A {
    template <typename T>
    struct MyPrint;
public:

    template <typename T>
    static void print() {
        // we wrap print in a struct to allow partial specializations.
        MyPrint<T>::print();
    }

private:
    // template for non-shared_ptr types
    template <typename T>
    struct MyPrint {
        static void print() {
            std::cout << "A::print<T>: " << T::getName() << std::endl;
        }
    };

    // partial specialization for shared_ptr types
    template <typename T>
    struct MyPrint<std::shared_ptr<T>> {
        // print() sub-specialization for other types
        template <typename U=T,
            typename std::enable_if<!(std::is_base_of<Base, U>::value), bool>::type = true>
        static void print() {
            std::cout << "(Non-Base Specialized)A::print<std::shared_ptr<T>>: " << T::getName() << std::endl;
        }

        // print() sub-specialization for types that inherit from a particular base class
        template <typename U=T,
            typename std::enable_if<std::is_base_of<Base, U>::value, bool>::type = true>
        static void print() {
            std::cout << "(Base specialized)A::print<std::shared_ptr<T>>: " << T::getName() << std::endl;
        }


    };
};

class B: public Base {
public:
    static std::string getName() { return "B"; }
};

class C {
public:
    static std::string getName() { return "C"; }
};

int main() {
    // regular old template function call
    A::print<B>(); 

    // regular old template function call
    A::print<C>(); 

    // specialization for shared_ptr Base subclasses
    A::print<std::shared_ptr<B>>(); 

    // specialization for shared_ptr non Base subclasses
    A::print<std::shared_ptr<C>>(); 

    return 0;
}

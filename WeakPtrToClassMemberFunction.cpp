#include <functional>
#include <memory>
#include <iostream>

class A{
public:
    void printSomething(int a) const;
    std::function<void(int)> otherPrint = std::bind(&A::printSomething, this, std::placeholders::_1);
};

void A::printSomething(int a) const {
    std::cout << "Printing Something: " << a << std::endl;
}

int main() {

    std::weak_ptr<std::function<void(int)>> weak_otherPrint {};
    {
        // A shared reference to a dynamically allocated instance of A, valid
        // only within the scope of this block
        std::shared_ptr<A> shrd_A { new A };

        // Works only because the binding is already stored in otherPrint, as a member of
        // A.
        weak_otherPrint = std::shared_ptr<std::function<void(int)>> { shrd_A, &shrd_A->otherPrint };
        std::weak_ptr<std::function<void(int)>> weak_otherPrint2 {
            std::shared_ptr<std::function<void(int)>>{shrd_A, &shrd_A->otherPrint}
        };

        // Only one shared pointer refers to the A instance; both function pointers 
        // to members are weak pointers.
        std::cout << weak_otherPrint.use_count() << std::endl;

        // I guess technically both weak_otherPrints are pointers function
        // pointers, and so need to be dereferenced before use?
        (*weak_otherPrint.lock())(8);
        (*weak_otherPrint2.lock())(2);

        // regular member access; nothing to see here.
        shrd_A->otherPrint(3);
    }

    std::cout << weak_otherPrint.use_count() << std::endl;
    std::cout << weak_otherPrint.lock() << std::endl;
}

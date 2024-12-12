/**
 *   Demonstrates the automatic creation and registration of factories
 * and factory methods during the static initialization phase of 
 * application startup. 
 * 
 *   Creating a new factory method simply requires the creation of 
 * a ResourceFactoryMethod subclass; the static registrator takes care
 * of making it visible to the top level Resource system
 */

#include <memory>
#include <string>
#include <vector>
#include <tuple>
#include <map>
#include <iostream>

// forces implementation of static function RegisterSelf, called here.
template<typename TRegisterable>
class Registrator {
public:
    Registrator() {
        std::cout << "Inside registrator ctor" << std::endl;
        TRegisterable::registerSelf();
    }
    void emptyFunc() {
    }
};

class IResource {
public:
    virtual ~IResource()=default;
};

class IResourceFactoryMethod;

class IResourceFactory {
public:
    virtual ~IResourceFactory()=default;
    std::map<std::string, std::unique_ptr<IResourceFactoryMethod>> mFactoryMethods {};
};

class IResourceFactoryMethod {
public:
    virtual ~IResourceFactoryMethod()=default;
    virtual std::unique_ptr<IResource> createResource(std::string params) = 0;
};

class ResourceDatabase {
public:
    static ResourceDatabase& getInstance() {
        static ResourceDatabase resourceDatabase {};
        return resourceDatabase;
    }

    static void RegisterFactory (std::string name, std::unique_ptr<IResourceFactory> pFactory) {
        getInstance().mFactories[name] = std::move(pFactory);
    }

    static void RegisterFactoryMethod (std::string resource, std::string method, std::unique_ptr<IResourceFactoryMethod> pFactoryMethod) {
        getInstance().mFactories[resource]->mFactoryMethods[method] = std::move(pFactoryMethod);
    }

    std::map<std::string, std::unique_ptr<IResourceFactory>> mFactories {};    

private:
    ResourceDatabase() = default;

};

template <typename TResource>
class ResourceFactory;

template <typename TDerived>
class Resource: public IResource {
public:
    static std::string getName () {
        return TDerived::getName();
    }
    static void registerSelf() {
        ResourceDatabase::RegisterFactory(getName(), std::make_unique<ResourceFactory<TDerived>>());
    }
protected:
    Resource(int explicitlyInitializeMe) { s_registrator.emptyFunc(); }
private:
    inline static Registrator<Resource<TDerived>> s_registrator = Registrator<Resource<TDerived>>();
};

template<typename TResource> 
class ResourceFactory: public IResourceFactory {
public:
    ResourceFactory() {
        std::cout << "Output of ctor" << std::endl;
    }
};

template<typename TResource, typename TDerivedMethod>
class ResourceFactoryMethod: public IResourceFactoryMethod {
public:
    static void registerSelf() {
        ResourceDatabase::RegisterFactoryMethod(TResource::getName(), TDerivedMethod::getName(), std::make_unique<TDerivedMethod>());
    }
protected:
    ResourceFactoryMethod(int explicitlyInitializeMe) {
        std::cout << "Output of factory method constructor" << std::endl;
        s_registrator.emptyFunc();
    }
private:
    static inline Registrator<ResourceFactoryMethod<TResource, TDerivedMethod>> s_registrator = Registrator<ResourceFactoryMethod<TResource, TDerivedMethod>>{};
};

// Specialization of the resource class, made automatically visible
// to the ResourceDatabase singleton by its registrator
class StringResource: public Resource<StringResource> {
public:
    StringResource(std::string params): Resource<StringResource>{0}, mResource {params} {}
    StringResource(): Resource<StringResource>{0} {}

    std::string mResource {};
    static std::string getName() {
        return "String";
    }
};

// Definition of a factory method, automatically made visible to the 
// factory for StringResources
class StringResourceFromString: public ResourceFactoryMethod<StringResource, StringResourceFromString> {
public:
    StringResourceFromString() 
    : ResourceFactoryMethod<StringResource, StringResourceFromString>{0} 
    {}

    static std::string getName() {
        return "FromString";
    }

    std::unique_ptr<IResource> createResource(std::string params) override {
        std::cout << "from FromString" << std::endl;
        std::unique_ptr<StringResource> strRes { std::make_unique<StringResource>() };
        strRes->mResource = params;
        return strRes;
    }
};

// Another factory method
class StringResourceFromInt: public ResourceFactoryMethod<StringResource, StringResourceFromInt> {
public:
    StringResourceFromInt()
    : ResourceFactoryMethod<StringResource, StringResourceFromInt>{0}
    {}
    
    static std::string getName() {
        return "FromInt";
    }
    std::unique_ptr<IResource> createResource(std::string params) override {
        std::cout << "from FromInt" << std::endl;
        std::unique_ptr<StringResource> strRes { std::make_unique<StringResource>() };
        strRes->mResource = mStrings[std::stoi(params)];
        return strRes;
    }

    const std::vector<std::string> mStrings {"Haha", "This should", "be fun.", "(I think)", "Woohooo"};
};

int main() {
    std::cout << "In main\n";

    std::cout << "Printing known resource types and their constructors: \n";
    for(const auto& pair: ResourceDatabase::getInstance().mFactories) {
        std::cout << "\tfactory:" << pair.first << std::endl;
        for(const auto& methodPair: pair.second->mFactoryMethods) {
            std::cout << "\t\tmethod:" << methodPair.first << std::endl;
        }
    }
    std::cout << std::endl;
    
    using typeMethodParams = std::tuple<std::string, std::string, std::string>;

    // These tuples act as serialized resource descriptions; they could
    // be read from a JSON or XML file.
    std::vector<typeMethodParams> resourceDescriptions { 
        {"String", "FromInt", "1"},
        {"String", "FromString", "Two"},
        {"String", "FromInt", "3"},
        {"String", "FromInt", "4"},
    };

    std::cout << "Printing resource descriptions and created resources: \n";
    for(const auto& description: resourceDescriptions) {
        // It's the factory methods job to deserialize resource descriptions
        std::shared_ptr<StringResource> strResource { 
            std::static_pointer_cast<StringResource, IResource>(
                ResourceDatabase::getInstance().mFactories[std::get<0>(description)]
                    ->mFactoryMethods[std::get<1>(description)]
                    ->createResource(
                        std::get<2>(description)
                    )
            )
        };
        std::cout << "\tresource description: " << std::get<0>(description) << ", " << std::get<1>(description) << ", " << std::get<2>(description) << "\n";
        std::cout << "\tcreated string: " << strResource->mResource << std::endl;
    }

    return 0;
}

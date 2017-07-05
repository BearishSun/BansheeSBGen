#include <string>
#include <vector>
#include <memory>

template<class Type>
class vector
{
	public:
	Type a;
};

typedef int INT32;

template<class Elem> class basic_string
{
	protected:
	Elem _elem;
};

typedef basic_string<char> string;
typedef basic_string<wchar_t> wstring;

namespace bs
{
template <class RetType, class... Args>
class TEvent
{
};

template <typename Signature>
class Event;

/** @copydoc TEvent */
template <class RetType, class... Args>
class Event<RetType(Args...) > : public TEvent <RetType, Args...>
{ };
}

class Component
{
public:
	virtual ~Component() {}
};

class __attribute__((annotate("something"))) toParse : public Component
	/** Meow */
{
	int f(int i, INT32 z, toParse b, const vector<int>& vec) {  
		if (i > 0) {
			return true;
		} else {
			return false;
		}
	}
};

////////////////////////////////////////

enum class __attribute__((annotate("se,pl:true,f:TestOutput"))) MyEnum
{
	a = 5,
	b = 8,
	c
};

enum class __attribute__((annotate("se,pl:true,f:TestOutput"))) MyEnum2
{
	a, b, c
};

enum __attribute__((annotate("se,pl:true,f:TestOutput"))) MyEnum3
{
	ME_A,
	ME_B,
	ME_C
};

struct __attribute__((annotate("se,pl:true,f:TestOutput"))) MyStruct
{
	int a;
	float b;
	float c = 10.0f;
};

struct __attribute__((annotate("se,pl:true,f:TestOutput"))) MyStruct2
{
	MyStruct2()
		:a(5), b(15.0f), c(10.0f)
	{ }
	
	MyStruct2(int a, float b, float c = 5.0f)
		:a(a), b(b), c(c)
	{
		this->a = a;
		this->b = b + c;
	}
	
	int a;
	float b;
	float c;
};

#define BS_SCRIPT_EXPORT(...) __attribute__((annotate("se," #__VA_ARGS__)))

class BS_SCRIPT_EXPORT(f:TestOutput) MyClass
{
	public:
	/**
	 * Creates a new mesh from an existing mesh data. Created mesh will match the vertex and index buffers described
	 * by the mesh data exactly. Mesh will have no sub-meshes.
	 *
	 * @param[in]	initialData		Vertex and index data to initialize the mesh with.
	 * @param[in]	desc			Descriptor containing the properties of the mesh to create. Vertex and index count,
	 *								vertex descriptor and index type properties are ignored and are read from provided
	 *								mesh data instead.
	 * @returns						Mesh.
	 */
	static int create(const int& initialData, const int& desc);
	
	BS_SCRIPT_EXPORT()
	bs::Event<void(int)> myEvent;
	
	BS_SCRIPT_EXPORT()
	static bs::Event<void(int)> myStaticEvent;
};

struct BS_SCRIPT_EXPORT(pl:true) ComplexStruct2
{
	MyStruct2 a;
	std::shared_ptr<MyClass> b;
	float c;
	std::vector<int> d;
};

struct BS_SCRIPT_EXPORT(pl:true) ComplexStruct
{
	MyStruct a;
	int b;
	std::string c;
	ComplexStruct2 d;
};

class BS_SCRIPT_EXPORT(f:TestOutput2,m:TestModule) MyClass2
{
	public:
	BS_SCRIPT_EXPORT()
	ComplexStruct getStruct();
	
	BS_SCRIPT_EXPORT()
	void setStruct(const ComplexStruct& value);
	
	BS_SCRIPT_EXPORT()
	std::vector<ComplexStruct> getStructArr();
	
	BS_SCRIPT_EXPORT()
	void setStructArr(const std::vector<ComplexStruct>& value);
	
	BS_SCRIPT_EXPORT()
	bs::Event<void(ComplexStruct)> myEvent;
	
	BS_SCRIPT_EXPORT()
	bs::Event<void(std::vector<ComplexStruct>)> myEvent2;
	
};
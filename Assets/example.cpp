#include <string>
#include <vector>
#include <memory>

template<class Type>
class vector
{
	public:
	Type a;
};

template<class T, class S = unsigned int>
class Flags
{
	S v;
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

enum BS_SCRIPT_EXPORT() FlgEnum
{
	a, b, c
};

typedef Flags<FlgEnum> FlgEnums;

/** Animation keyframe, represented as an endpoint of a cubic hermite spline. */
template <class T>
struct TKeyframe
{
	T value; /**< Value of the key. */
	T inTangent; /**< Input tangent (going from the previous key to this one) of the key. */
	T outTangent; /**< Output tangent (going from this key to next one) of the key. */
	float time; /**< Position of the key along the animation spline. */
};

template class BS_SCRIPT_EXPORT(n:Keyframe,pl:true) TKeyframe<float>;

struct BS_SCRIPT_EXPORT(pl:true) KeyframeContainer
{
	TKeyframe<float> keyframe;
};

template <class T>
class TAnimationCurve
{
	BS_SCRIPT_EXPORT(n:Evaluate)
	T evaluate(float time, bool loop = true) const;
	
	BS_SCRIPT_EXPORT(n:KeyFrame,p:getter)
	const std::vector<TKeyframe<T>>& getKeyFrames() const;
};

template class BS_SCRIPT_EXPORT(n:AnimationCurve) TAnimationCurve<float>;

struct BS_SCRIPT_EXPORT(m:Animation,pl:true) RootMotion
{
	RootMotion() { }
	RootMotion(const TAnimationCurve<float>& position, const TAnimationCurve<float>& rotation)
		:position(position), rotation(rotation)
	{ }

	/** Animation curve representing the movement of the root bone. */
	TAnimationCurve<float> position;

	/** Animation curve representing the rotation of the root bone. */
	TAnimationCurve<float> rotation;
};


class BS_SCRIPT_EXPORT(f:TestOutput) MyClass
{
	public:
	/**
	 * @native
	 * Testing native docs!!
	 *
	 * Multiparagaph!
	 * @endnative
	 * @script
	 * Creates a new mesh from an existing mesh data. Created mesh will match the vertex and index buffers described
	 * by the mesh data exactly. Mesh will have no sub-meshes.
	 * @endscript
	 *
	 * Use in both documentations
	 *
	 * @param[in]	initialData		Vertex and index data to initialize the mesh with.
	 * @param[in]	desc			Descriptor containing the properties of the mesh to create. Vertex and index count,
	 *								vertex descriptor and index type properties are ignored and are read from provided
	 *								mesh data instead.
	 * @returns						Mesh.
	 */
	BS_SCRIPT_EXPORT() int create(const int& initialData, const int& desc);
	
	/** Some docs. */
	BS_SCRIPT_EXPORT()
	void setSomething(float t);
	
	/** @copydoc setSomething() */
	BS_SCRIPT_EXPORT()
	float getSomething() const;
	
	BS_SCRIPT_EXPORT()
	FlgEnums getEnum() const;	
	
	BS_SCRIPT_EXPORT()
	void setEnum(FlgEnums e) const;		
	
	BS_SCRIPT_EXPORT()
	bs::Event<void(int)> myEvent;
	
	BS_SCRIPT_EXPORT()
	bs::Event<void(FlgEnums)> myEnumEvent;
	
	BS_SCRIPT_EXPORT()
	static bs::Event<void(int)> myStaticEvent;
};

class BS_SCRIPT_EXPORT() StaticClass
{
	BS_SCRIPT_EXPORT()
	static StaticClass get();
	
	BS_SCRIPT_EXPORT()
	static void set(const StaticClass& v);
};

struct BS_SCRIPT_EXPORT(pl:true) MyStruct3
{
	float a;
	FlgEnums b;
	int c;
};

struct BS_SCRIPT_EXPORT(pl:true) MyStruct4
{
	MyStruct4() {}
	
	MyStruct4(MyStruct b)
		:a(b)
	{ }
	
	MyStruct a;
	std::string c;
};

template <class T>
struct TNamedAnimationCurve
{
	std::string name;
	T value;
};

template class BS_SCRIPT_EXPORT(m:Animation,n:NamedFloatCurve) TNamedAnimationCurve<float>;

struct BS_SCRIPT_EXPORT(pl:true) ComplexStruct3
{
	MyStruct2 a;
	std::shared_ptr<MyClass> b;
	float c;
	FlgEnums d;
	std::vector<int> e;
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
	MyClass2(bool a, const std::shared_ptr<MyClass>& b = nullptr);
	
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
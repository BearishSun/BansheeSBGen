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

#define BS_SCRIPT_EXPORT(...) __attribute__((annotate("se," #__VA_ARGS__)))

namespace bs
{
template <class RetType, class... Args>
class TEvent
{
};

template<class Elem> class TResourceHandle
{
	
};

class Resource
{
	
};

class BS_SCRIPT_EXPORT() Texture : public Resource
{
	
};

template <typename Signature>
class Event;

/** @copydoc TEvent */
template <class RetType, class... Args>
class Event<RetType(Args...) > : public TEvent <RetType, Args...>
{ };

	struct Degree;

	class Radian
	{
	public:
		explicit Radian(float r = 0.0f) : mRad(r) {}
		Radian(const Degree& d);
		Radian& operator= (const float& f) { mRad = f; return *this; }
		Radian& operator= (const Radian& r) { mRad = r.mRad; return *this; }
		Radian& operator= (const Degree& d);

		/** Returns the value of the angle in degrees. */
		float valueDegrees() const;

		/** Returns the value of the angle in radians. */
		float valueRadians() const { return mRad; }

		/** Wraps the angle in [0, 2 *  PI) range. */
		Radian wrap();

        const Radian& operator+ () const { return *this; }
		Radian operator+ (const Radian& r) const { return Radian (mRad + r.mRad); }
		Radian operator+ (const Degree& d) const;
		Radian& operator+= (const Radian& r) { mRad += r.mRad; return *this; }
		Radian& operator+= (const Degree& d);
		Radian operator- () const { return Radian(-mRad); }
		Radian operator- (const Radian& r) const { return Radian (mRad - r.mRad); }
		Radian operator- (const Degree& d) const;
		Radian& operator-= (const Radian& r) { mRad -= r.mRad; return *this; }
		Radian& operator-= (const Degree& d);
		Radian operator* (float f) const { return Radian (mRad * f); }
        Radian operator* (const Radian& f) const { return Radian (mRad * f.mRad); }
		Radian& operator*= (float f) { mRad *= f; return *this; }
		Radian operator/ (float f) const { return Radian (mRad / f); }
		Radian& operator/= (float f) { mRad /= f; return *this; }

		friend Radian operator* (float lhs, const Radian& rhs) { return Radian(lhs * rhs.mRad); }
		friend Radian operator/ (float lhs, const Radian& rhs) { return Radian(lhs / rhs.mRad); }
		friend Radian operator+ (Radian& lhs, float rhs) { return Radian(lhs.mRad + rhs); }
		friend Radian operator+ (float lhs, const Radian& rhs) { return Radian(lhs + rhs.mRad); }
		friend Radian operator- (const Radian& lhs, float rhs) { return Radian(lhs.mRad - rhs); }
		friend Radian operator- (const float lhs, const Radian& rhs) { return Radian(lhs - rhs.mRad); }

		bool operator<  (const Radian& r) const { return mRad <  r.mRad; }
		bool operator<= (const Radian& r) const { return mRad <= r.mRad; }
		bool operator== (const Radian& r) const { return mRad == r.mRad; }
		bool operator!= (const Radian& r) const { return mRad != r.mRad; }
		bool operator>= (const Radian& r) const { return mRad >= r.mRad; }
		bool operator>  (const Radian& r) const { return mRad >  r.mRad; }

	private:
		float mRad;
	};
	
class Math
{
public:
	static constexpr float PI = 3.14f;	
};

struct Spring;

struct BS_SCRIPT_EXPORT(pl:true) LimitAngularRange
	{
		/** Constructs an empty limit. */
		LimitAngularRange()
		{ }

		/**
		 * Constructs a hard limit. Once the limit is reached the movement of the attached bodies will come to a stop.
		 * 
		 * @param	lower		Lower angle of the limit. Must be less than @p upper.
		 * @param	upper		Upper angle of the limit. Must be more than @p lower.
		 * @param	contactDist	Distance from the limit at which it becomes active. Allows the solver to activate earlier
		 *						than the limit is reached to avoid breaking the limit. Specify -1 for the default.
		 */
		LimitAngularRange(Radian lower, Radian upper, float contactDist = -1.0f)
			:lower(lower), upper(upper)
		{ }

		/**
		 * Constructs a soft limit. Once the limit is reached the bodies will bounce back according to the resitution
		 * parameter and will be pulled back towards the limit by the provided spring.
		 * 
		 * @param	lower		Lower angle of the limit. Must be less than @p upper.
		 * @param	upper		Upper angle of the limit. Must be more than @p lower.
		 * @param	spring		Spring that controls how are the bodies pulled back towards the limit when they breach it.
		 * @param	restitution	Controls how do objects react when the limit is reached, values closer to zero specify
		 *						non-ellastic collision, while those closer to one specify more ellastic (i.e bouncy)
		 *						collision. Must be in [0, 1] range.
		 */
		LimitAngularRange(Radian lower, Radian upper, const Spring& spring, float restitution = 0.0f)
			:lower(lower), upper(upper)
		{ }

		bool operator==(const LimitAngularRange& other) const
		{
			return lower == other.lower && upper == other.upper;
		}

		/** Lower angle of the limit. Must be less than #upper. */
		Radian lower = Radian(0.0f);

		/** Upper angle of the limit. Must be less than #lower. */
		Radian upper = Radian(0.0f);
	};
}

class Component
{
public:
	virtual ~Component() {}
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

enum BS_SCRIPT_EXPORT() FlgEnum
{
	a, b, c
};

struct Vector3I
{
	int32_t x, y, z;
	
	constexpr Vector3I(int32_t x, int32_t y, int32_t z)
		: x(x), y(y), z(z)
		{ }
};

enum BS_SCRIPT_EXPORT(n:MeshTopology,m:Rendering) DrawOperationType
{
	DOT_POINT_LIST		BS_SCRIPT_EXPORT(n:PointList)		= 1,
	DOT_LINE_LIST		BS_SCRIPT_EXPORT(n:LineList)		= 2,
	DOT_LINE_STRIP		BS_SCRIPT_EXPORT(n:LineStrip)		= 3,
	DOT_TRIANGLE_LIST	BS_SCRIPT_EXPORT(n:TriangleList)	= 4,
	DOT_TRIANGLE_STRIP	BS_SCRIPT_EXPORT(n:TriangleStrip)	= 5,
	DOT_TRIANGLE_FAN	BS_SCRIPT_EXPORT(n:TriangleFan)		= 6
};

struct BS_SCRIPT_EXPORT(pl:true) SubMesh
{
	SubMesh()
		: indexOffset(0), indexCount(0), drawOp(DOT_TRIANGLE_LIST)
	{ }

	SubMesh(uint32_t indexOffset, uint32_t indexCount, DrawOperationType drawOp):
		indexOffset(indexOffset), indexCount(indexCount), drawOp(drawOp)
	{ }

	uint32_t indexOffset;
	uint32_t indexCount;
	DrawOperationType drawOp;
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

template <>
struct BS_SCRIPT_EXPORT(n:KeyframeInt,pl:true) TKeyframe<INT32>
{
	INT32 value; /**< Value of the key. */
	float time; /**< Position of the key along the animation spline. */
};

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

struct BS_SCRIPT_EXPORT(pl:true) Str1
{
	float a;
	int c;
};

class BS_SCRIPT_EXPORT(m:Image) ColorGradient
{
public:
	BS_SCRIPT_EXPORT()
	ColorGradient() = default;

	BS_SCRIPT_EXPORT()
	int evaluate(float t) const;
};

struct BS_SCRIPT_EXPORT(pl:true) Str2 : public Str1
{
	int cda;
	BS_SCRIPT_EXPORT(ex:true)
	float cdb;
	std::wstring cdc;
};

class BS_SCRIPT_EXPORT() Cmp1 : public Component
{

};

class BS_SCRIPT_EXPORT() Cmp2 : public Cmp1
{

};

class BS_SCRIPT_EXPORT(f:TestOutput) MyClass
{
	public:
	/**
	 * @native
	 * Testing native docs!! @p initialData is a parameter!
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
	BS_SCRIPT_EXPORT() int create(const int& initialData, const int& desc, unsigned long long superlong = 0xFFFFFFFFFFFFFFFF);
	
	BS_SCRIPT_EXPORT() ColorGradient anotherTest();
	
	/** Some stuff to comment @p dft some more stuff. */
	BS_SCRIPT_EXPORT() void tst(const Vector3I& dft = Vector3I(1, 1, 1));
	
	BS_SCRIPT_EXPORT()
	std::vector<bs::TResourceHandle<bs::Texture>> textures;
	
	BS_SCRIPT_EXPORT()
	std::vector<std::wstring> getIdentifiers() const;
	
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

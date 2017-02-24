template<class Type>
class vector
{
	public:
	Type a;
};

typedef int INT32;

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

class __attribute__((annotate("se,f:TestOutput"))) MyClass
{
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
};
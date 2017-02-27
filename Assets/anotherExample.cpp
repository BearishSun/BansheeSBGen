namespace stdd
{
	template<class T>
	class shared_ptr
	{
		public:
		T a;
	};
}

template <typename T>
using SPtr = stdd::shared_ptr<T>;

class ResourceHandleBase
{
	public:
		virtual ~ResourceHandleBase() { }

	protected:
		ResourceHandleBase()
			:mRefCount(0)
		{ }

		void throwIfNotLoaded() const { }
		
	protected:
		int mRefCount;
};

template <bool WeakHandle>
class TResourceHandleBase : public ResourceHandleBase
{ };

template<>
class TResourceHandleBase<true> : public ResourceHandleBase
{
	public:
		virtual ~TResourceHandleBase() { }

	protected:
		void addRef() { };
		void releaseRef() { };
};

template<>
class TResourceHandleBase<false> : public ResourceHandleBase
{
	public:
		virtual ~TResourceHandleBase() { }

	protected:
		void addRef() { mRefCount++; };
		void releaseRef() { mRefCount--; };
};

template <typename T, bool WeakHandle>
class TResourceHandle : public TResourceHandleBase<WeakHandle>
{
	public:
		TResourceHandle()
		{ }

		virtual ~TResourceHandle()
		{ }

		T* get() const 
		{ 
			return &mValue;
		}

	protected:
		T mValue;
};

template <typename T>
using ResourceHandle = TResourceHandle<T, false>;

template <typename T>
using WeakResourceHandle = TResourceHandle<T, true>;

template <typename T>
class GameObjectHandle;

class GameObjectHandleBase
{
public:
	GameObjectHandleBase() { }
};

template <typename T>
class GameObjectHandle : public GameObjectHandleBase
{
public:
	/**	Constructs a new empty handle. */
	GameObjectHandle()
		:GameObjectHandleBase()
	{}

	T* get() const 
	{ 
		return &mValue;
	}
	
private:
	T mValue;
};


typedef ResourceHandle<int> HResInt;
typedef GameObjectHandle<int> HGOInt;

class __attribute__((annotate("se,f:TestOutput"))) MyClass2
{
	__attribute__((annotate("se")))
	void testSPtr(const SPtr<int>& param)
	{
		
	}
	
	__attribute__((annotate("se")))
	void testResHandle(const HResInt& param)
	{
		
	}
	
	__attribute__((annotate("se")))
	void testGOHandle(const HGOInt& param)
	{
		
	}
};
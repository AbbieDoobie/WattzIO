#include "IHudReveal.h"

// CommonLibF4RD declares BSScript::IStackCallbackFunctor's virtual destructor without defining it,
// so deriving from it leaves the derived vtable referencing an unresolved symbol at link time.
// Defining it here is safe: the base contributes only BSIntrusiveRefCounted's refcount, and this
// applies only to callback objects this plugin allocates.
namespace RE::BSScript
{
	IStackCallbackFunctor::~IStackCallbackFunctor() = default;
}

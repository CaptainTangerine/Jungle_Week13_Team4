#include "Physics/NvClothSystem.h"

#include "Core/Logging/Log.h"

#if WITH_NVCLOTH
#include "NvCloth/Callbacks.h"
#include "NvCloth/Factory.h"

#include <foundation/PxErrorCallback.h>
#include <malloc.h>

namespace
{
	class FNvClothAllocator final : public physx::PxAllocatorCallback
	{
	public:
		void* allocate(size_t Size, const char* /*TypeName*/, const char* /*Filename*/, int /*Line*/) override
		{
			return _aligned_malloc(Size, 16);
		}

		void deallocate(void* Ptr) override
		{
			_aligned_free(Ptr);
		}
	};

	class FNvClothErrorCallback final : public physx::PxErrorCallback
	{
	public:
		void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
		{
			UE_LOG("[NvCloth][Error %d] %s (%s:%d)", static_cast<int>(Code), Message, File, Line);
		}
	};

	class FNvClothAssertHandler final : public nv::cloth::PxAssertHandler
	{
	public:
		void operator()(const char* Expression, const char* File, int Line, bool& Ignore) override
		{
			UE_LOG("[NvCloth][Assert] %s (%s:%d)", Expression, File, Line);
			Ignore = true;
		}
	};

	FNvClothAllocator GNvClothAllocator;
	FNvClothErrorCallback GNvClothErrorCallback;
	FNvClothAssertHandler GNvClothAssertHandler;
}
#endif

bool FNvClothSystem::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

#if WITH_NVCLOTH
	UE_LOG("[NvCloth] Initializing callbacks");
	nv::cloth::InitializeNvCloth(&GNvClothAllocator, &GNvClothErrorCallback, &GNvClothAssertHandler, nullptr);

	UE_LOG("[NvCloth] Creating CPU factory");
	Factory = NvClothCreateFactoryCPU();
	if (!Factory)
	{
		UE_LOG("[NvCloth] Failed to create CPU factory");
		return false;
	}

	bInitialized = true;
	UE_LOG("[NvCloth] CPU factory initialized (Factory=%p, CUDA=%d, DX=%d)",
		Factory,
		NvClothCompiledWithCudaSupport() ? 1 : 0,
		NvClothCompiledWithDxSupport() ? 1 : 0);
	return true;
#else
	UE_LOG("[NvCloth] Disabled (WITH_NVCLOTH=0)");
	return false;
#endif
}

void FNvClothSystem::Shutdown()
{
#if WITH_NVCLOTH
	if (Factory)
	{
		NvClothDestroyFactory(Factory);
		Factory = nullptr;
		UE_LOG("[NvCloth] CPU factory destroyed");
	}
#endif

	bInitialized = false;
}

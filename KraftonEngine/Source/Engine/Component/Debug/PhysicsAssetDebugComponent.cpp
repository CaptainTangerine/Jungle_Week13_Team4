#include "PhysicsAssetDebugComponent.h"

#include "Object/Reflection/ObjectFactory.h"
#include "Render/Proxy/PhysicsAssetDebugSceneProxy.h"

FPrimitiveSceneProxy* UPhysicsAssetDebugComponent::CreateSceneProxy()
{
	return new FPhysicsAssetDebugSceneProxy(this);
}

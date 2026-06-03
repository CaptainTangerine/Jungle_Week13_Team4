// Copyright Epic Games, Inc. All Rights Reserved.
#include "ShapeComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Object.h"
#include "Serialization/Archive.h"
#include "Render/Proxy/ShapeSceneProxy.h"
#include "Physics/Asset/BodySetup.h"

#include <cstring>

HIDE_FROM_COMPONENT_LIST(UShapeComponent)

UShapeComponent::UShapeComponent()
{
	bCastShadow = false;
}

FPrimitiveSceneProxy* UShapeComponent::CreateSceneProxy()
{
	return new FShapeSceneProxy(this);
}

UBodySetup* UShapeComponent::GetBodySetup()
{
	if (!ShapeBodySetup)
	{
		ShapeBodySetup = UObjectManager::Get().CreateObject<UBodySetup>(this);
	}
	if (ShapeBodySetup)
	{
		// 매 호출마다 현재 치수로 재구성 — extent 변경(SetBoxExtent 등) 후 재초기화에서 최신값 반영.
		ShapeBodySetup->AggGeom.SphereElems.clear();
		ShapeBodySetup->AggGeom.BoxElems.clear();
		ShapeBodySetup->AggGeom.SphylElems.clear();
		ShapeBodySetup->TriMesh.Clear();
		BuildShapeBodySetup(*ShapeBodySetup);
	}
	return ShapeBodySetup;
}

void UShapeComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "ShapeColor") == 0 || strcmp(PropertyName, "bDrawOnlyIfSelected") == 0
		|| strcmp(PropertyName, "Shape Color") == 0 || strcmp(PropertyName, "Draw Only If Selected") == 0)
	{
		MarkRenderStateDirty();
	}
}
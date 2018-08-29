// Copyright blackw 2015-2020

#pragma once

#include "Engine.h"
#include "SandboxTerrainMeshComponent.h"
#include "VoxelDcMeshComponent.generated.h"




/**
*
*/
UCLASS( ClassGroup = (Custom), meta = (BlueprintSpawnableComponent) )
class UNREALSANDBOXTERRAIN_API UVoxelDcMeshComponent : public USandboxTerrainMeshComponent {
	GENERATED_UCLASS_BODY()


public:

	//virtual void BeginDestroy();

	virtual void BeginPlay() override;

	//virtual void EndPlay(const EEndPlayReason::Type EndPlayReason);

protected:
	TVoxelData* VoxelData;

};
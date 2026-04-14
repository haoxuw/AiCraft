// CreatureForge — Body Compiler (interface)
//
// Runs on every morphology edit and produces the derived artifacts.
// Incremental: hashes each subgraph and rebuilds only dirty passes.
//
// Implementation in BodyCompiler.cpp (Stage 1 — starts with spine
// and implicit-surface skin only; parts, rig, physics come online in
// later stages).

#pragma once

#include "CoreMinimal.h"
#include "MorphologyGraph.h"

class UDynamicMesh;
class USkeletalMesh;
class UPhysicsAsset;
class UMaterialInterface;

namespace CreatureForge
{
	// Inputs per-recompile. The compiler stashes last-seen hashes in
	// FCompileCache and early-outs passes whose hash is unchanged.
	struct FCompileCache
	{
		uint32 LastSpineHash    = 0;
		uint32 LastPartsHash    = 0;
		uint32 LastPaintHash    = 0;
		uint32 LastBehaviorHash = 0;

		// Derived outputs — kept across edits so unchanged passes can
		// skip rebuilding.
		TObjectPtr<UDynamicMesh>        SkinMesh;
		TObjectPtr<USkeletalMesh>       Skeleton;
		TObjectPtr<UPhysicsAsset>       Physics;
		TObjectPtr<UMaterialInterface>  Material;
	};

	struct FCompileResult
	{
		bool bSpineRebuilt    = false;
		bool bPartsRebuilt    = false;
		bool bPaintRebuilt    = false;
		bool bBehaviorRebuilt = false;
		float CompileMs = 0.f;
	};

	// Single entry point. Thread-safe: the caller owns the cache and
	// is responsible for not calling Compile() concurrently on the
	// same cache.
	FCompileResult Compile(const FMorphologyGraph& Graph, FCompileCache& Cache);
}

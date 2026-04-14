// CreatureForge — Part Asset
//
// The palette entry. One UCreaturePartAsset per unique part shape
// (e.g. "Insect-leg-01", "Frog-eye", "Dragon-wing"). Hot-reloadable:
// drop a new .uasset under /Game/Parts/ and it appears in the palette
// automatically (palette enumerates by asset registry, not a list).
//
// Parts are NOT UObjects in the save file — FPartInstance holds a
// TSoftObjectPtr. A missing/deleted part renders as a red placeholder
// in the editor and is skipped at cook time.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CreaturePartAsset.generated.h"

UENUM(BlueprintType)
enum class EPartCategory : uint8
{
	Mouth,    // ingestion — drives bite force, jaw animation
	Eye,      // sight sensor — drives FOV, sight range
	Ear,      // hearing — drives hear range
	Nose,     // smell — drives forage radius
	Foot,     // walk locomotion — drives leg count/stride
	Fin,      // swim locomotion — drives swim speed
	Wing,     // fly locomotion — drives lift area
	Grasper,  // manipulators — drives carry capacity
	Weapon,   // offense — drives damage
	Spike,    // defense — drives armor
	Detail,   // purely cosmetic
};

// A single numeric capability contribution this part adds. Summed
// across all instances at compile time and exposed in the stat panel
// with traceability back to the contributing parts.
USTRUCT(BlueprintType)
struct FPartCapability
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) FName Stat;       // "BiteForce", "SightRange", ...
	UPROPERTY(EditAnywhere) float Value = 0.f;
};

// Deform range clamps — editor sliders respect these.
USTRUCT(BlueprintType)
struct FPartDeformLimits
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) FVector2D Scale       = FVector2D(0.25f, 3.0f);
	UPROPERTY(EditAnywhere) FVector2D Taper       = FVector2D(-1.0f, 1.0f);
	UPROPERTY(EditAnywhere) FVector2D Bend        = FVector2D(-1.0f, 1.0f);
	UPROPERTY(EditAnywhere) FVector2D Twist       = FVector2D(-1.0f, 1.0f);
	UPROPERTY(EditAnywhere) FVector2D Thickness   = FVector2D(-1.0f, 1.0f);
};

UCLASS(BlueprintType)
class UCreaturePartAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	// Stable name — used in save files. Never rename post-publish;
	// instead create a new asset and deprecate the old one.
	UPROPERTY(EditAnywhere) FName PartId;

	UPROPERTY(EditAnywhere) EPartCategory Category = EPartCategory::Detail;

	// Visual mesh. Triangle mesh, origin at the attach anchor, +Z
	// pointing along the "away-from-body" surface normal.
	UPROPERTY(EditAnywhere) TObjectPtr<class UStaticMesh> Mesh;

	// If true, the body compiler unions this as a *hard* surface
	// (boolean mesh CSG). If false, the part is added as an SDF blob
	// and blends smoothly into the body. Hard = insect/mech look;
	// soft = blob/spore look.
	UPROPERTY(EditAnywhere) bool bHardSurface = false;

	// DNA cost — caps how many parts fit in a creature.
	UPROPERTY(EditAnywhere, meta=(ClampMin="0")) int32 DnaCost = 1;

	// Where on itself a *child* part can attach (e.g. a mouth can
	// hold a tooth). Each socket is a name + local transform. Empty
	// for most parts.
	UPROPERTY(EditAnywhere) TMap<FName, FTransform> ChildSockets;

	// What this part contributes to the creature's stats.
	UPROPERTY(EditAnywhere) TArray<FPartCapability> Capabilities;

	// Per-instance deform ranges.
	UPROPERTY(EditAnywhere) FPartDeformLimits DeformLimits;

	// IK anchor bone name emitted by the auto-rigger (e.g. "foot_L",
	// "wing_tip_R"). Empty for non-limb parts.
	UPROPERTY(EditAnywhere) FName IkAnchorBone = NAME_None;
};

// Paint layer resource — referenced by FPaintLayer::Layer. Cheap
// holder for base textures + noise parameters; the real blend logic
// lives in the material graph.
UCLASS(BlueprintType)
class UPaintLayerAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere) FName LayerId;
	UPROPERTY(EditAnywhere) TObjectPtr<class UTexture2D> BaseColor;
	UPROPERTY(EditAnywhere) TObjectPtr<class UTexture2D> Normal;
	UPROPERTY(EditAnywhere) float Roughness = 0.6f;
	UPROPERTY(EditAnywhere) float Metallic  = 0.0f;
	// Procedural noise pattern uniforms — material reads these.
	UPROPERTY(EditAnywhere) FVector4 NoiseParams = FVector4(1, 1, 0, 0);
};

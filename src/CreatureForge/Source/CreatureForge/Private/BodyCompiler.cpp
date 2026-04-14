// BodyCompiler — Stage 1 scaffold.
//
// Current scope: hash-diff the inputs and stamp which passes would
// run. Mesh/rig/physics generation lands in later stages. This file
// exists now so the incremental-recompile wiring is already in place
// when the first real pass arrives.

#include "BodyCompiler.h"

namespace CreatureForge
{
	FCompileResult Compile(const FMorphologyGraph& Graph, FCompileCache& Cache)
	{
		const double T0 = FPlatformTime::Seconds();
		FCompileResult R;

		const uint32 SpineH    = Graph.HashSpine();
		const uint32 PartsH    = Graph.HashParts();
		const uint32 PaintH    = Graph.HashPaint();
		const uint32 BehaviorH = Graph.HashBehavior();

		if (SpineH != Cache.LastSpineHash)
		{
			// TODO(stage1): build implicit surface from spine →
			// march to UDynamicMesh; assign to Cache.SkinMesh.
			R.bSpineRebuilt = true;
			Cache.LastSpineHash = SpineH;
		}
		if (PartsH != Cache.LastPartsHash || R.bSpineRebuilt)
		{
			// TODO(stage2): attach parts; CSG-union hard parts,
			// SDF-union soft parts.
			R.bPartsRebuilt = true;
			Cache.LastPartsHash = PartsH;
		}
		if (PaintH != Cache.LastPaintHash)
		{
			// TODO(stage6): compose paint layers into the material
			// instance parameters.
			R.bPaintRebuilt = true;
			Cache.LastPaintHash = PaintH;
		}
		if (BehaviorH != Cache.LastBehaviorHash)
		{
			// TODO(stage9): compile behavior graph to StateTree.
			R.bBehaviorRebuilt = true;
			Cache.LastBehaviorHash = BehaviorH;
		}

		R.CompileMs = float((FPlatformTime::Seconds() - T0) * 1000.0);
		return R;
	}
}

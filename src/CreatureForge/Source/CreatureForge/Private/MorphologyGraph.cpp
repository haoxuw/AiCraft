#include "MorphologyGraph.h"

// --------------------------------------------------------------------
// Hashing — cheap CRC over each subgraph. The body compiler uses these
// to early-out passes whose inputs didn't change. Treat the hashes as
// opaque keys: the absolute values have no meaning across runs.
// --------------------------------------------------------------------

namespace {
	FORCEINLINE uint32 HashFloat(uint32 h, float v) {
		return HashCombine(h, GetTypeHash(v));
	}
	FORCEINLINE uint32 HashVec(uint32 h, const FVector& v) {
		h = HashFloat(h, (float)v.X);
		h = HashFloat(h, (float)v.Y);
		h = HashFloat(h, (float)v.Z);
		return h;
	}
	FORCEINLINE uint32 HashRot(uint32 h, const FRotator& r) {
		h = HashFloat(h, r.Pitch);
		h = HashFloat(h, r.Yaw);
		h = HashFloat(h, r.Roll);
		return h;
	}
}

uint32 FMorphologyGraph::HashSpine() const
{
	uint32 h = GetTypeHash(Spine.Tension);
	for (const FSpineNode& n : Spine.Nodes)
	{
		h = HashVec(h, n.Position);
		h = HashFloat(h, n.Thickness);
		h = HashFloat(h, n.TwistRadians);
	}
	return h;
}

uint32 FMorphologyGraph::HashParts() const
{
	uint32 h = 0;
	for (const FPartInstance& p : Parts)
	{
		h = HashCombine(h, GetTypeHash(p.InstanceId));
		h = HashCombine(h, GetTypeHash(p.Part.ToSoftObjectPath()));
		h = HashFloat(h, p.SplineT);
		h = HashFloat(h, p.AngleAroundSpine);
		h = HashRot(h, p.LocalRotation);
		h = HashFloat(h, p.Deform.Scale);
		h = HashFloat(h, p.Deform.Taper);
		h = HashFloat(h, p.Deform.Bend);
		h = HashFloat(h, p.Deform.Twist);
		h = HashFloat(h, p.Deform.Thickness);
		h = HashFloat(h, p.Asymmetry);
	}
	return h;
}

uint32 FMorphologyGraph::HashPaint() const
{
	uint32 h = 0;
	for (const FPaintLayer& L : Paint.Layers)
	{
		h = HashCombine(h, GetTypeHash(L.LayerId));
		h = HashCombine(h, GetTypeHash(L.Layer.ToSoftObjectPath()));
		h = HashFloat(h, L.Tint.R); h = HashFloat(h, L.Tint.G);
		h = HashFloat(h, L.Tint.B); h = HashFloat(h, L.Tint.A);
		h = HashFloat(h, L.Opacity);
		h = HashCombine(h, GetTypeHash((uint8)L.BlendMode));
		h = HashCombine(h, GetTypeHash(L.MaskId));
	}
	return h;
}

uint32 FMorphologyGraph::HashBehavior() const
{
	uint32 h = GetTypeHash(Behavior.RootNodeId);
	for (const FBehaviorNode& n : Behavior.Nodes)
	{
		h = HashCombine(h, GetTypeHash(n.NodeId));
		h = HashCombine(h, GetTypeHash((uint8)n.Kind));
		for (const FGuid& g : n.Next)  h = HashCombine(h, GetTypeHash(g));
		for (float v : n.Params)       h = HashFloat(h, v);
	}
	return h;
}

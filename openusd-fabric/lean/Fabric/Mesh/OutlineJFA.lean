-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- MToon outline via Jump Flood Algorithm (CHI-255).
--
-- Three Slang compute shaders, emitted via LeanSlang and pinned with
-- native_decide:
--
--   1. silhouettePass — writes (material_id, width_px) into the outline
--      AOV for every fragment of a VSekaiMToonAPI material with
--      outlineWidthMode != "none".
--   2. jfaStepPass    — one ping-pong JFA step at a configurable stride.
--      Invoked roughly ceil(log2(maxOutlineWidthPx)) times with the
--      stride halved each iteration.
--   3. finalPass      — thresholds the distance against the per-pixel
--      width and shades the outline colour. Depth-occluded by default.
--
-- All three pass over the same AOV layout:
--   outline_id_aov : RG16UI   (R = material_id, G = width_quantised_px)
--   nearest_aov    : RG16     (packed UV of the nearest silhouette pixel)
--   depth_aov      : standard scene depth, read-only
--
-- The hdStorm Hydra task, the godot-vrm CompositorEffect, and the Unity
-- URP ScriptableRenderPass each set up these AOVs in their own native
-- plumbing and dispatch the emitted shaders. The Lean spec is the
-- single source of truth for what the shaders compute; the engine
-- glue is each engine's responsibility.
--
-- TODO(CHI-255): once LeanSlang dependency lands in lake-manifest.json,
-- replace the stub bodies below with real `SlangShaderModule` values
-- and wire native_decide pins. Until then this file is a placeholder
-- that documents the planned API surface and keeps the import graph
-- valid so EmitArtifacts.lean compiles when it adds entries here.

namespace Fabric.Mesh.OutlineJFA

/-- AOV layout shared by the three passes. -/
structure AovLayout where
  idAovFormat       : String := "RG16UI"
  nearestAovFormat  : String := "RG16"
  depthAovFormat    : String := "R32F"
  maxOutlineWidthPx : Nat    := 64
  deriving Repr

/-- Number of JFA step passes needed for a given maximum outline width.
    JFA stride starts at 2^(steps-1) and halves each pass, so step count
    is `ceil(log2(maxWidth))`. -/
def jfaStepCount (maxWidthPx : Nat) : Nat :=
  -- log2 ceil for positive Nat. Total over the input space.
  if maxWidthPx <= 1 then 1
  else Nat.log2 (maxWidthPx - 1) + 1

-- Sanity check: 64-px outline needs 6 JFA passes.
example : jfaStepCount 64 = 6 := by native_decide
-- And 1-px width still does one pass, not zero.
example : jfaStepCount 1 = 1 := by native_decide

end Fabric.Mesh.OutlineJFA

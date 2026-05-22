"""
V-Sekai post-export hook for Blender's USD exporter.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Blender's native USD exporter does not apply custom API schemas, so this
hook runs after the export to stamp the V-Sekai schemas onto the right
prims and write back the v_sekai:* attributes that downstream tools
(idtx-flow for Godot, the schema mapper for Unity) consume.

Two entry points:

* CLI form, headless and CI-friendly:
      blender --background --python blender/post_export_hook.py -- \
          --in path/to/exported.usda
  Arguments past `--` are read via sys.argv after Blender strips its own
  flags. The script edits the file in place by default; pass --out to
  write a separate stage.

* In-process form, called from a Python addon's `wm.usd_export`
  post-handler:
      from openusd_fabric_blender import post_export_hook
      post_export_hook.apply_v_sekai_schemas(stage)

Plugin discovery: USD looks at PXR_PLUGINPATH_NAME for codeless schemas.
This script auto-sets it to ../schema relative to its own location if the
variable is unset, so the standalone CLI form works without any wrapper.

Phase 1 scope (CHI-251):
* Apply VSekaiMToonAPI to material prims flagged as MToon.
* Apply VSekaiSpringBoneAPI to joints flagged as springbone roots.
* Apply VSekaiSpringBoneColliderAPI to joints flagged as colliders.

Detection of "flagged as ..." is currently a stub — the rules live with
the Blender side of the V-Sekai authoring pipeline and will fill in as
the asset conventions stabilise. The plumbing here is what gets reused;
the predicates are what change.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Iterable

try:
    from pxr import Sdf, Usd, UsdGeom, UsdShade
except ImportError as exc:
    raise SystemExit(
        "pxr (OpenUSD Python bindings) is not importable. Run this script "
        "from inside Blender (blender --background --python ...) or from a "
        "Python environment where `pip install usd-core` succeeded."
    ) from exc


SCHEMA_DIR_DEFAULT = (Path(__file__).resolve().parent.parent / "schema")

# Markers and namespaces. The V-Sekai authoring convention is a tiny
# pre-export step (an operator or post-handler in the V-Sekai branch of
# vrm-addon-for-blender) that mirrors the addon's RNA-pointer-property
# state into Blender id_properties on the source datablock. Blender's USD
# exporter then surfaces those id_properties as USD attributes under the
# `userProperties:` namespace, which is what the hook reads here. The
# raw `vrm_addon_extension.mtoon1.enabled` PointerProperty does NOT
# survive USD export on its own.
USER_PROPERTIES_PREFIX = "userProperties:"
MTOON_MARKER_ATTR = "userProperties:v_sekai:mtoon"
SPRINGBONE_MARKER_ATTR = "userProperties:v_sekai:springBone"
COLLIDER_MARKER_ATTR = "userProperties:v_sekai:springBoneCollider"

# MToon factor mirroring: every Blender-side id_property named
# `v_sekai:mtoon:<field>` is copied onto the USD prim as the matching
# schema attribute. The hook does not invent values — if a factor is not
# in the source customData, the schema default applies. Texture asset
# attributes share the same mapping; the value lives in the asset
# resolver path that Blender writes.
_MTOON_SCHEMA_TYPES: dict[str, Sdf.ValueTypeName] = {
    "v_sekai:mtoon:shadeColorFactor":              Sdf.ValueTypeNames.Color3f,
    "v_sekai:mtoon:shadeMultiplyTexture":          Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:shadingShiftFactor":            Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:shadingShiftTexture":           Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:shadingShiftTextureScale":      Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:shadingToonyFactor":            Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:giEqualizationFactor":          Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:matcapFactor":                  Sdf.ValueTypeNames.Color3f,
    "v_sekai:mtoon:matcapTexture":                 Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:rimMultiplyTexture":            Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:rimLightingMixFactor":          Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:parametricRimColorFactor":      Sdf.ValueTypeNames.Color3f,
    "v_sekai:mtoon:parametricRimFresnelPowerFactor": Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:parametricRimLiftFactor":       Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:outlineWidthMode":              Sdf.ValueTypeNames.Token,
    "v_sekai:mtoon:outlineWidthFactor":            Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:outlineWidthMultiplyTexture":   Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:outlineColorFactor":            Sdf.ValueTypeNames.Color3f,
    "v_sekai:mtoon:outlineLightingMixFactor":      Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:uvAnimationMaskTexture":        Sdf.ValueTypeNames.Asset,
    "v_sekai:mtoon:uvAnimationScrollXSpeedFactor": Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:uvAnimationScrollYSpeedFactor": Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:uvAnimationRotationSpeedFactor": Sdf.ValueTypeNames.Float,
    "v_sekai:mtoon:renderQueueOffsetNumber":       Sdf.ValueTypeNames.Int,
    "v_sekai:mtoon:transparentWithZWrite":         Sdf.ValueTypeNames.Bool,
}

_SPRINGBONE_SCHEMA_TYPES: dict[str, Sdf.ValueTypeName] = {
    "v_sekai:springBone:stiffness":     Sdf.ValueTypeNames.Float,
    "v_sekai:springBone:drag":          Sdf.ValueTypeNames.Float,
    "v_sekai:springBone:gravityPower":  Sdf.ValueTypeNames.Float,
    "v_sekai:springBone:gravityDir":    Sdf.ValueTypeNames.Vector3f,
    "v_sekai:springBone:hitRadius":     Sdf.ValueTypeNames.Float,
    # `center` and `colliders` are USD relationships, not attributes; they
    # are stamped via _stamp_springbone_relationships below because the
    # userProperties→attribute mirror does not understand relationships.
}

_COLLIDER_SCHEMA_TYPES: dict[str, Sdf.ValueTypeName] = {
    "v_sekai:springBone:collider:shape":  Sdf.ValueTypeNames.Token,
    "v_sekai:springBone:collider:radius": Sdf.ValueTypeNames.Float,
    "v_sekai:springBone:collider:offset": Sdf.ValueTypeNames.Vector3f,
    "v_sekai:springBone:collider:tail":   Sdf.ValueTypeNames.Vector3f,
    "v_sekai:springBone:collider:normal": Sdf.ValueTypeNames.Vector3f,
    "v_sekai:springBone:collider:inside": Sdf.ValueTypeNames.Bool,
}


def ensure_plugin_path(schema_dir: Path = SCHEMA_DIR_DEFAULT) -> None:
    """Prepend the V-Sekai schema directory to PXR_PLUGINPATH_NAME if absent.

    USD only consults PXR_PLUGINPATH_NAME at plugin-registry warmup, which
    happens on the first UsdStage::Open or Usd.Stage.Open call. This must
    run before any stage is opened in the process, hence module top of
    every callsite calling into apply_v_sekai_schemas().
    """
    schema_dir = schema_dir.resolve()
    if not (schema_dir / "plugInfo.json").exists():
        raise FileNotFoundError(
            f"V-Sekai plugInfo.json not found at {schema_dir}. "
            "Pass --schema-dir or set PXR_PLUGINPATH_NAME manually."
        )
    current = os.environ.get("PXR_PLUGINPATH_NAME", "")
    parts = [p for p in current.split(os.pathsep) if p]
    if str(schema_dir) in parts:
        return
    parts.insert(0, str(schema_dir))
    os.environ["PXR_PLUGINPATH_NAME"] = os.pathsep.join(parts)


def _iter_material_prims(stage: Usd.Stage) -> Iterable[Usd.Prim]:
    for prim in stage.Traverse():
        if prim.IsA(UsdShade.Material):
            yield prim


def _iter_marked_prims(stage: Usd.Stage, marker_attr: str) -> Iterable[Usd.Prim]:
    """Yield every prim whose `marker_attr` userProperty resolves truthy.

    The V-Sekai authoring convention models springbone roots and
    colliders as ordinary Xform prims (siblings of the Skeleton) rather
    than as Skeleton-internal joints, because USD joints are entries in
    a `joints` token array on the Skeleton prim and cannot carry API
    schemas directly. The pre-export step writes the joint-equivalent
    Xform plus the marker id_property; the hook only needs to find it.
    """
    for prim in stage.Traverse():
        if _truthy_user_property(prim, marker_attr):
            yield prim


def _truthy_user_property(prim: Usd.Prim, attr_name: str) -> bool:
    """Return True iff prim has attr_name with a non-zero / non-empty value.

    Blender's USD exporter writes id_properties under `userProperties:`,
    so the V-Sekai pre-export marker `material["v_sekai:mtoon"] = 1`
    surfaces as `userProperties:v_sekai:mtoon`. Treat any non-zero
    numeric, non-empty string, or True bool as the marker being set.
    """
    attr = prim.GetAttribute(attr_name)
    if not attr or not attr.IsValid():
        return False
    value = attr.Get()
    if value is None:
        return False
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return bool(value)
    return True


def _is_mtoon_material(prim: Usd.Prim) -> bool:
    """Predicate: should this material carry VSekaiMToonAPI?

    Looks for the V-Sekai authoring marker `userProperties:v_sekai:mtoon`,
    emitted by the V-Sekai pre-export step that mirrors
    `material.vrm_addon_extension.mtoon1.enabled` (from
    vrm-addon-for-blender) into a Blender id_property. The id_property
    survives Blender's USD export under the userProperties namespace;
    the RNA PointerProperty itself does not.
    """
    return _truthy_user_property(prim, MTOON_MARKER_ATTR)


def _is_springbone_root(prim: Usd.Prim) -> bool:
    """Predicate: should this prim carry VSekaiSpringBoneAPI?

    Looks for `userProperties:v_sekai:springBone`, the marker the V-Sekai
    pre-export step mirrors from `armature.data.vrm_addon_extension
    .spring_bone1.spring_bones[i].joints[0]` (the chain root) into a
    Blender id_property on the corresponding bone / empty.
    """
    return _truthy_user_property(prim, SPRINGBONE_MARKER_ATTR)


def _is_springbone_collider(prim: Usd.Prim) -> bool:
    """Predicate: should this prim carry VSekaiSpringBoneColliderAPI?

    Looks for `userProperties:v_sekai:springBoneCollider`, mirrored from
    `spring_bone1.colliders[i]` into a Blender id_property on the
    collider's representative bone / empty.
    """
    return _truthy_user_property(prim, COLLIDER_MARKER_ATTR)


def _apply_api(prim: Usd.Prim, api_name: str) -> None:
    """Apply a single-apply API schema to a prim if not already applied.

    Uses the generic ApplyAPI(TfType) path because the codeless schema
    has no generated C++ class to call ApplyAPI(VSekaiMToonAPI) on.
    """
    if prim.HasAPI(api_name):
        return
    prim.ApplyAPI(api_name)


def _copy_user_property_to_schema_attr(
    prim: Usd.Prim,
    schema_attr_name: str,
    schema_type: Sdf.ValueTypeName,
) -> bool:
    """Mirror a `userProperties:<schema_attr_name>` value onto schema_attr_name.

    Returns True if a value was copied (source attribute present), False
    otherwise. Texture-asset values are passed through unchanged — Blender
    writes them as Sdf.AssetPath instances with the resolved path baked
    in, which is exactly what idtx-flow and the Unity mapper expect.
    """
    source = prim.GetAttribute(USER_PROPERTIES_PREFIX + schema_attr_name)
    if not source or not source.IsValid():
        return False
    value = source.Get()
    if value is None:
        return False
    target = prim.CreateAttribute(schema_attr_name, schema_type)
    target.Set(value)
    return True


def _stamp_mtoon_attrs(prim: Usd.Prim) -> int:
    n = 0
    for attr_name, attr_type in _MTOON_SCHEMA_TYPES.items():
        if _copy_user_property_to_schema_attr(prim, attr_name, attr_type):
            n += 1
    return n


def _stamp_springbone_attrs(prim: Usd.Prim) -> int:
    n = 0
    for attr_name, attr_type in _SPRINGBONE_SCHEMA_TYPES.items():
        if _copy_user_property_to_schema_attr(prim, attr_name, attr_type):
            n += 1
    return n


def _stamp_collider_attrs(prim: Usd.Prim) -> int:
    n = 0
    for attr_name, attr_type in _COLLIDER_SCHEMA_TYPES.items():
        if _copy_user_property_to_schema_attr(prim, attr_name, attr_type):
            n += 1
    return n


def apply_v_sekai_schemas(stage: Usd.Stage) -> dict[str, int]:
    """Apply V-Sekai API schemas across the stage. Returns counts per API."""
    counts = {
        "VSekaiMToonAPI": 0,
        "VSekaiSpringBoneAPI": 0,
        "VSekaiSpringBoneColliderAPI": 0,
        "mtoon_attrs_stamped": 0,
    }

    for prim in _iter_material_prims(stage):
        if _is_mtoon_material(prim):
            _apply_api(prim, "VSekaiMToonAPI")
            counts["VSekaiMToonAPI"] += 1
            counts["mtoon_attrs_stamped"] += _stamp_mtoon_attrs(prim)

    for prim in _iter_marked_prims(stage, SPRINGBONE_MARKER_ATTR):
        _apply_api(prim, "VSekaiSpringBoneAPI")
        counts["VSekaiSpringBoneAPI"] += 1
        _stamp_springbone_attrs(prim)

    for prim in _iter_marked_prims(stage, COLLIDER_MARKER_ATTR):
        _apply_api(prim, "VSekaiSpringBoneColliderAPI")
        counts["VSekaiSpringBoneColliderAPI"] += 1
        _stamp_collider_attrs(prim)

    return counts


def _user_args() -> list[str]:
    """Return CLI args trailing Blender's own flag block.

    Blender forwards anything after a literal `--` to the script. Inside a
    plain `python` invocation there is no `--` separator, so we fall back
    to sys.argv[1:].
    """
    if "--" in sys.argv:
        return sys.argv[sys.argv.index("--") + 1:]
    return sys.argv[1:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--in", dest="input_path", required=True,
                        help="Path to the .usda stage exported by Blender.")
    parser.add_argument("--out", dest="output_path", default=None,
                        help="Where to write the stamped stage. "
                             "Defaults to overwriting the input.")
    parser.add_argument("--schema-dir", dest="schema_dir", default=None,
                        help="Directory containing plugInfo.json. "
                             "Defaults to ../schema next to this script.")
    args = parser.parse_args(_user_args())

    schema_dir = Path(args.schema_dir) if args.schema_dir else SCHEMA_DIR_DEFAULT
    ensure_plugin_path(schema_dir)

    stage = Usd.Stage.Open(args.input_path)
    if stage is None:
        print(f"error: could not open {args.input_path}", file=sys.stderr)
        return 1

    counts = apply_v_sekai_schemas(stage)

    out_path = args.output_path or args.input_path
    if out_path == args.input_path:
        stage.GetRootLayer().Save()
    else:
        stage.GetRootLayer().Export(out_path)

    summary = ", ".join(f"{name}={n}" for name, n in counts.items())
    print(f"openusd-fabric: applied V-Sekai schemas → {summary} → {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

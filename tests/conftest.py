"""
Copyright (C) 2025 yuygfgg

This file is part of Vapoursynth-llvmexpr.

Vapoursynth-llvmexpr is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Vapoursynth-llvmexpr is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Vapoursynth-llvmexpr.  If not, see <https://www.gnu.org/licenses/>.
"""

import os
from pathlib import Path
import sys
import pytest
import vapoursynth as vs
import numpy as np
from typing import Callable

core = vs.core

def _load_llvmexpr_plugin() -> None:
    if hasattr(core, "llvmexpr"):
        return

    repo_root = Path(__file__).resolve().parents[1]

    env_plugin_path = os.environ.get("LLVMEXPR_PLUGIN_PATH")
    if env_plugin_path:
        plugin_path = Path(env_plugin_path)
        if not plugin_path.is_absolute():
            plugin_path = (repo_root / plugin_path).resolve()
        if plugin_path.is_file():
            core.std.LoadPlugin(path=str(plugin_path))
            return
        raise RuntimeError(f"LLVMEXPR_PLUGIN_PATH does not exist: {plugin_path}")

    env_builddir = os.environ.get("LLVMEXPR_BUILDDIR")
    builddir = Path(env_builddir) if env_builddir else (repo_root / "builddir")
    if not builddir.is_absolute():
        builddir = (repo_root / builddir).resolve()

    if sys.platform.startswith("win"):
        preferred_names = ("libllvmexpr.dll", "llvmexpr.dll")
        allowed_suffixes = (".dll",)
    elif sys.platform == "darwin":
        preferred_names = ("libllvmexpr.dylib", "llvmexpr.dylib")
        allowed_suffixes = (".dylib",)
    else:
        preferred_names = ("libllvmexpr.so", "llvmexpr.so")
        allowed_suffixes = (".so",)

    for name in preferred_names:
        candidate = builddir / name
        if candidate.is_file():
            core.std.LoadPlugin(path=str(candidate))
            return

    if builddir.is_dir():
        matches = sorted(
            p
            for p in builddir.iterdir()
            if p.is_file()
            and p.suffix.lower() in allowed_suffixes
            and "llvmexpr" in p.name.lower()
        )
        if matches:
            core.std.LoadPlugin(path=str(matches[0]))
            return

    raise RuntimeError(
        "Failed to locate llvmexpr plugin for tests. "
        f"Looked in {builddir} and did not find {', '.join(preferred_names)}. "
        "Build the plugin into ./builddir or set LLVMEXPR_PLUGIN_PATH."
    )


_load_llvmexpr_plugin()

_PRESET_FORMAT_NAMES = [
    "GRAYS",
    "GRAY8",
    "GRAY16",
    "RGB24",
    "RGBH",
    "RGBS",
    "YUV420P8",
    "YUV420P16",
    "YUV422P16",
    "YUV444P10",
]


def _backfill_preset_format_constants() -> None:
    preset_enum = getattr(vs, "PresetFormat", None) or getattr(vs, "PresetVideoFormat", None)
    if preset_enum is None:
        return

    members: dict[str, object] = {}
    preset_members = getattr(preset_enum, "__members__", None)
    if isinstance(preset_members, dict):
        for member_name, member in preset_members.items():
            members[str(member_name).upper()] = member
    else:
        for member_name in dir(preset_enum):
            if member_name.startswith("_"):
                continue
            members[member_name.upper()] = getattr(preset_enum, member_name)

    for name in _PRESET_FORMAT_NAMES:
        if hasattr(vs, name):
            continue
        member = members.get(name)
        if member is not None:
            setattr(vs, name, member)


_backfill_preset_format_constants()


BACKENDS = ["Expr", "VkExpr"]


def get_expr_func(backend: str) -> Callable[..., vs.VideoNode]:
    """Returns the appropriate expression function based on backend name."""
    if backend == "Expr":
        return core.llvmexpr.Expr
    elif backend == "VkExpr":
        return core.llvmexpr.VkExpr
    else:
        raise ValueError(f"Unknown backend: {backend}")


@pytest.fixture(params=BACKENDS)
def backend(request) -> str:
    """Fixture that parametrizes tests to run with both backends."""
    return request.param


@pytest.fixture
def expr_func(backend: str) -> Callable[..., vs.VideoNode]:
    """Fixture that returns the appropriate expression function for the current backend."""
    return get_expr_func(backend)


@pytest.fixture(scope="module")
def ramp_clip_factory():
    """Factory for creating ramp clips."""
    width, height = 4, 4
    base = core.std.BlankClip(format=vs.GRAYS, width=width, height=height, color=0.0)

    def ramp_frame(n, f):
        fout = f.copy()
        arr = np.asarray(fout[0])
        for y in range(height):
            for x in range(width):
                arr[y, x] = y * width + x
        return fout

    return core.std.ModifyFrame(base, clips=base, selector=ramp_frame)


@pytest.fixture
def ramp_clip(ramp_clip_factory) -> vs.VideoNode:
    """Ramp clip fixture for boundary condition tests."""
    return ramp_clip_factory


@pytest.fixture(scope="module")
def subsampled_ramp_clip_factory():
    """Factory for creating subsampled ramp clips."""
    width, height = 4, 4
    base = core.std.BlankClip(format=vs.YUV420P8, width=width, height=height)
    u_ramp_expr = "Y 2 * X +"
    return base, u_ramp_expr


@pytest.fixture
def subsampled_ramp_clip(subsampled_ramp_clip_factory, expr_func) -> vs.VideoNode:
    """Subsampled ramp clip fixture for subsampled plane access tests."""
    base, u_ramp_expr = subsampled_ramp_clip_factory
    return expr_func([base], ["", u_ramp_expr])

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

import pytest
import vapoursynth as vs
import numpy as np
from typing import Callable

core = vs.core


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

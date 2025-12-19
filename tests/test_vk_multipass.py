
import vapoursynth as vs
import pytest

core = vs.core

def test_vk_multipass_chaining():
    clip = core.std.BlankClip(width=16, height=16, format=vs.GRAYS, color=[0.5])
    core.llvmexpr.VkExpr(clips=[clip], expr="x")


    clip = core.std.BlankClip(width=128, height=128, format=vs.GRAYS, color=[0.5])
    
    # Expr: "x 0.5 + ## x buf0 +"
    # Stage 1: 0.5 + 0.5 = 1.0 (buf0)
    # Stage 2: 0.5 + 1.0 = 1.5
    processed = core.llvmexpr.VkExpr(
        clips=[clip], 
        expr="x 0.5 + ## x buf0 +"
    )
    
    f = processed.get_frame(0)
    p = f[0]
    assert p[0, 0] == pytest.approx(1.5, rel=1e-5), f"Expected 1.5, got {p[0, 0]}"

def test_vk_multipass_multi_buffer():
    clip = core.std.BlankClip(width=128, height=128, format=vs.GRAYS, color=[0.1])
    
    # "x 0.1 + ## x 0.2 + ## buf0 buf1 +"
    # Stage 1 (buf0): 0.1 + 0.1 = 0.2
    # Stage 2 (buf1): 0.1 + 0.2 = 0.3
    # Stage 3 (out):  0.2 + 0.3 = 0.5
    
    processed = core.llvmexpr.VkExpr(
        clips=[clip],
        expr="x 0.1 + ## x 0.2 + ## buf0 buf1 +"
    )
    
    f = processed.get_frame(0)
    p = f[0]
    assert p[0, 0] == pytest.approx(0.5, rel=1e-5), f"Expected 0.5, got {p[0, 0]}"

def test_vk_multipass_complex_math():
    clip = core.std.BlankClip(width=64, height=64, format=vs.GRAYS, color=[0.5])
    expr = "x pi * sin ## buf0 pi * cos ## buf0 buf1 +"
    
    processed = core.llvmexpr.VkExpr(
        clips=[clip],
        expr=expr
    )
    
    f = processed.get_frame(0)
    p = f[0]
    # Allow some tolerance for GPU float precision
    assert p[0, 0] == pytest.approx(0.0, abs=1e-5), f"Expected 0.0, got {p[0, 0]}"

def test_vk_multipass_deep_chain():
    clip = core.std.BlankClip(width=32, height=32, format=vs.GRAYS, color=[1.0])
    expr = "x 1 + ## buf0 1 + ## buf1 1 + ## buf2 1 + ## buf3 1 +"
    
    processed = core.llvmexpr.VkExpr(
        clips=[clip],
        expr=expr
    )
    
    f = processed.get_frame(0)
    p = f[0]
    assert p[0, 0] == pytest.approx(6.0, rel=1e-5), f"Expected 6.0, got {p[0, 0]}"

def test_vk_multipass_buffer_relative():
    # Gradient clip: 0 1 2 3 ...
    clip = core.std.BlankClip(width=16, height=16, format=vs.GRAYS, color=[0.0])
    clip = core.llvmexpr.Expr(clips=[clip], expr="X Y +") 
    
    # buf0[1,0] should get the value of (X+1, Y)
    # If X=0, Y=0, buf0[0,0]=0, buf0[1,0]=1
    expr = "x ## buf0[1,0]"
    
    processed = core.llvmexpr.VkExpr(clips=[clip], expr=expr)
    f = processed.get_frame(0)
    p = f[0]
    
    # At (0,0), expected is buf0[1,0] which is 1.0 (since 1+0=1)
    # At (5,5), expected is buf0[6,5] which is 11.0
    assert p[0, 0] == pytest.approx(1.0, rel=1e-5)
    assert p[5, 5] == pytest.approx(11.0, rel=1e-5)

def test_vk_multipass_buffer_absolute():
    clip = core.std.BlankClip(width=16, height=16, format=vs.GRAYS, color=[0.0])
    clip = core.llvmexpr.Expr(clips=[clip], expr="X Y +")
    
    expr = "x ## X 1 + Y buf0[]"
    expr = "x ## X 1 + Y buf0[]"
    
    processed = core.llvmexpr.VkExpr(clips=[clip], expr=expr)
    f = processed.get_frame(0)
    p = f[0]
    
    assert p[0, 0] == pytest.approx(1.0, rel=1e-5)
    assert p[5, 5] == pytest.approx(11.0, rel=1e-5)

def test_vk_multipass_buffer_boundary():
    clip = core.std.BlankClip(width=16, height=16, format=vs.GRAYS, color=[0.0])
    clip = core.llvmexpr.Expr(clips=[clip], expr="X") # Only depends on X
    
    expr_mirror = "x ## buf0[-1,0]:m"
    expr_clamp  = "x ## buf0[-1,0]:c"
    
    processed_m = core.llvmexpr.VkExpr(clips=[clip], expr=expr_mirror)
    processed_c = core.llvmexpr.VkExpr(clips=[clip], expr=expr_clamp)
    
    pm = processed_m.get_frame(0)[0]
    pc = processed_c.get_frame(0)[0]
    
    assert pm[0, 0] == pytest.approx(0.0, abs=1e-5)
    assert pc[0, 0] == pytest.approx(0.0, abs=1e-5)

def test_vk_multipass_convolution():
    # Impulse at (5,5)
    clip = core.std.BlankClip(width=16, height=16, format=vs.GRAYS, color=[0.0])
    clip = core.llvmexpr.Expr([clip], "X 5 = Y 5 = and 1 0 ?")
    
    # 3x1 Horizontal blur then 1x3 Vertical blur -> 3x3 blur
    # Stage 0 (H-Blur): (src0[-1,0] + src0[0,0] + src0[1,0]) / 3
    # Stage 1 (V-Blur): (buf0[0,-1] + buf0[0,0] + buf0[0,1]) / 3
    
    expr = "x x[-1,0] + x[1,0] + 3 / ## buf0 buf0[0,-1] + buf0[0,1] + 3 /"
    
    processed = core.llvmexpr.VkExpr(clips=[clip], expr=expr)
    f = processed.get_frame(0)
    p = f[0]
    
    # Center (5,5) should be (1/3)/3 = 1/9
    assert p[5, 5] == pytest.approx(1/9, rel=1e-5)
    # Neighbors (e.g. 4,4) should also have values (expanded impulse)
    assert p[4, 4] == pytest.approx(1/9, rel=1e-5)
    assert p[6, 6] == pytest.approx(1/9, rel=1e-5)
    assert p[3, 5] == pytest.approx(0.0, abs=1e-5)
    assert p[5, 3] == pytest.approx(0.0, abs=1e-5) 

def test_bufN_availability():
    if not hasattr(core, 'llvmexpr'):
        pytest.skip("llvmexpr not available")
    
    clip = core.std.BlankClip(width=16, height=16, format=vs.GRAYS)
    
    with pytest.raises(vs.Error, match="Invalid token: buf0"):
        core.llvmexpr.Expr([clip], "buf0")
    
    with pytest.raises(vs.Error, match="Invalid token: buf0"):
        core.llvmexpr.SingleExpr([clip], "buf0")

def test_bufN_index_validation():
    clip = core.std.BlankClip(width=16, height=16, format=vs.GRAYS)
    
    with pytest.raises(vs.Error, match="Invalid buffer index in token: buf0"):
        core.llvmexpr.VkExpr([clip], "buf0")
    
    with pytest.raises(vs.Error, match="Invalid buffer index in token: buf1"):
        core.llvmexpr.VkExpr([clip], "x ## buf1")

    core.llvmexpr.VkExpr([clip], "x ## buf0")

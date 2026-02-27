import vapoursynth as vs
import time
import os

os.environ["LLVMEXPR_TIME_PASSES"] = "1"

expr = "x x x[-1,-1] x[0,-1] x[1,-1] x[-1,0] x x[1,0] x[-1,1] x[0,1] x[1,1] + + + + + + + + 9 / - 1.5 * 1.0 x[-1,-1] x[0,-1] x[1,-1] x[-1,0] x x[1,0] x[-1,1] x[0,1] x[1,1] + + + + + + + + 9 / x[-2,-2] x[-2,0] + x[-2,2] + x[0,-2] + x[0,2] + x[2,-2] + x[2,0] + x[2,2] + 9 / - abs x x[-1,-1] x[0,-1] x[1,-1] x[-1,0] x x[1,0] x[-1,1] x[0,1] x[1,1] + + + + + + + + 9 / - abs x[-1,-1] x[0,-1] x[1,-1] x[-1,0] x x[1,0] x[-1,1] x[0,1] x[1,1] + + + + + + + + 9 / x[-2,-2] x[-2,0] + x[-2,2] + x[0,-2] + x[0,2] + x[2,-2] + x[2,0] + x[2,2] + 9 / - abs max 0.01 + / 0 max 5.0 * 0.1 * 0.95 min - * 1.0 x[-1,-1] x[1,-1] -1 * x[-1,0] 2 * x[1,0] -2 * x[-1,1] x[1,1] -1 * + + + + + abs 2 pow x[-1,-1] x[0,-1] 2 * x[1,-1] x[-1,1] -1 * x[0,1] -2 * x[1,1] -1 * + + + + + abs 2 pow + sqrt 0.01 * 0.3 min - 1.0 x[-1,-1] x - 2 pow x[0,-1] x - 2 pow + x[1,-1] x - 2 pow + x[-1,0] x - 2 pow + x[1,0] x - 2 pow + x[-1,1] x - 2 pow + x[0,1] x - 2 pow + x[1,1] x - 2 pow + 8 / 0.005 * 0.3 min - * * + x[-1,-1] x[0,-1] min x[1,-1] min x[-1,0] min x[1,0] min x[-1,1] min x[0,1] min x[1,1] min x[-1,-1] x[0,-1] max x[1,-1] max x[-1,0] max x[1,0] max x[-1,1] max x[0,1] max x[1,1] max clamp"

clip = vs.core.std.BlankClip(None, 3840, 2160, vs.YUV420P16, length=500)

llvm = vs.core.llvmexpr.Expr(clip, " ".join([expr] * 5) + " max max max max", opt_level=1)

llvm.get_frame(0)

del os.environ["LLVMEXPR_TIME_PASSES"]
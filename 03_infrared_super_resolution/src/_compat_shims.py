"""
本地无网络环境的兼容垫片：让官方 hat_arch.py 在缺少 basicsr / einops 时也能 import。
仅当真包缺失时注入桩；GPU 实例上 `pip install basicsr einops` 后这些桩自动不生效。

注意：桩里的 rearrange 是通用实现(reshape+permute)，与真 einops 数值完全一致，
trunc_normal_/to_2tuple 直接取自 timm，ARCH_REGISTRY 只是个空装饰器——
都不影响权重数值，故本地验证结果可信。
"""
import sys
import types


def _make_rearrange():
    import re
    import torch

    def _parse(side):
        # 返回 [(group_names_tuple), ...]，group 可能是单名或 (a b c)
        groups, i, s = [], 0, side.strip()
        tokens = re.findall(r"\(|\)|[A-Za-z_]\w*", s)
        cur, in_group = None, False
        for t in tokens:
            if t == "(":
                cur, in_group = [], True
            elif t == ")":
                groups.append(tuple(cur)); cur, in_group = None, False
            else:
                if in_group:
                    cur.append(t)
                else:
                    groups.append((t,))
        return groups

    def rearrange(tensor, pattern, **lengths):
        left, right = pattern.split("->")
        lg, rg = _parse(left), _parse(right)
        assert tensor.dim() == len(lg), f"维度不匹配 {tensor.dim()} vs {len(lg)}"
        # 1) 把每个左侧 group 拆成单轴，得到展开后的 shape 与轴名顺序
        sizes, names = [], []
        for dim, group in enumerate(lg):
            total = tensor.shape[dim]
            known = {n: lengths[n] for n in group if n in lengths}
            prod_known = 1
            for v in known.values():
                prod_known *= v
            unknown = [n for n in group if n not in lengths]
            assert len(unknown) <= 1, f"group {group} 有多个未知轴"
            if unknown:
                known[unknown[0]] = total // prod_known
            for n in group:
                sizes.append(known[n]); names.append(n)
        x = tensor.reshape(*sizes)
        # 2) 按右侧展开顺序 permute
        right_names = [n for group in rg for n in group]
        perm = [names.index(n) for n in right_names]
        x = x.permute(*perm).contiguous()
        # 3) 按右侧 group 合并
        out_shape, idx = [], 0
        name_size = {n: sizes[names.index(n)] for n in names}
        for group in rg:
            p = 1
            for n in group:
                p *= name_size[n]
            out_shape.append(p)
        return x.reshape(*out_shape)

    return rearrange


def install():
    # ---- timm.layers 兜底 ----
    # 部分官方 arch(如 ATD)用 `from timm.layers import ...`;老版 timm 只有
    # `timm.models.layers`。缺 timm.layers 时别名过去,保证提交在老版 timm 也能 import。
    try:
        import timm.layers  # noqa
    except Exception:
        try:
            import timm.models.layers as _tl
            sys.modules["timm.layers"] = _tl
        except Exception:
            pass

    # ---- einops ----
    try:
        import einops  # noqa
    except ImportError:
        m = types.ModuleType("einops")
        m.rearrange = _make_rearrange()
        sys.modules["einops"] = m

    # ---- huggingface_hub.PyTorchModelHubMixin ----
    # hit_sir_arch.py 仅把它当基类继承(为了 hub 上传/下载便利)，我们从不调用这些功能。
    # 真包存在(实例上随 timm 装了)就用真的；缺失(评测环境)则注入空 Mixin，提交不引入新依赖。
    try:
        from huggingface_hub import PyTorchModelHubMixin  # noqa
    except Exception:
        hf = types.ModuleType("huggingface_hub")

        class PyTorchModelHubMixin:  # 空 Mixin，仅满足继承
            pass

        hf.PyTorchModelHubMixin = PyTorchModelHubMixin
        sys.modules["huggingface_hub"] = hf

    # ---- basicsr.utils.registry.ARCH_REGISTRY + arch_util ----
    try:
        import basicsr  # noqa
        import basicsr.utils.registry  # noqa
        import basicsr.archs.arch_util  # noqa
    except ImportError:
        # to_2tuple / trunc_normal_ —— 多级兜底,最终落到 torch 内置,零外部依赖
        try:
            from timm.layers import to_2tuple, trunc_normal_
        except Exception:
            try:
                from timm.models.layers import to_2tuple, trunc_normal_
            except Exception:
                import collections.abc
                from torch.nn.init import trunc_normal_  # torch>=1.5 自带

                def to_2tuple(x):
                    if isinstance(x, collections.abc.Iterable) and not isinstance(x, str):
                        return tuple(x)
                    return (x, x)

        class _Reg:
            def register(self, *a, **k):
                def deco(cls):
                    return cls
                return deco

        basicsr = types.ModuleType("basicsr")
        utils = types.ModuleType("basicsr.utils")
        registry = types.ModuleType("basicsr.utils.registry")
        registry.ARCH_REGISTRY = _Reg()
        archs = types.ModuleType("basicsr.archs")
        arch_util = types.ModuleType("basicsr.archs.arch_util")
        arch_util.to_2tuple = to_2tuple
        arch_util.trunc_normal_ = trunc_normal_
        utils.registry = registry
        archs.arch_util = arch_util
        basicsr.utils = utils
        basicsr.archs = archs
        sys.modules["basicsr"] = basicsr
        sys.modules["basicsr.utils"] = utils
        sys.modules["basicsr.utils.registry"] = registry
        sys.modules["basicsr.archs"] = archs
        sys.modules["basicsr.archs.arch_util"] = arch_util

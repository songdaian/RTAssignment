# 普通体积次表面路径追踪：公式与代码对应

这份文档描述仓库中第一版体积次表面路径追踪器。目标不是先做扩散近似或控制变量，而是先得到目标估计器 `F`：光线进入一个闭合物体后，在均匀参与介质中进行真实的随机游走。

## 1. 当前实现的范围

- 闭合、法线朝外的三角网格；
- 均匀 RGB 吸收与散射系数；
- 光滑介质边界；
- Henyey-Greenstein（HG）相函数；
- 相机发出的单向路径追踪；
- RGB hero-channel 自由程采样。

场景材质写成：

```json
{
  "bsdf": "homogeneous_medium",
  "reflectance": "1_1_1.png",
  "sigmaA": "0.10 0.35 0.80",
  "sigmaS": "4.00 4.00 4.00",
  "g": "0.0",
  "intIOR": "1.0",
  "extIOR": "1.0"
}
```

`reflectance` 是边界透射颜色，通常使用白色。`sigmaA` 和 `sigmaS` 的单位是场景长度单位的倒数，所以模型缩放会改变它的光学厚度。

## 2. 介质系数

对每个颜色通道定义：

```text
sigma_t = sigma_a + sigma_s
```

- `sigma_a`：吸收系数。值越大，光越容易在介质中消失；
- `sigma_s`：散射系数。值越大，光越频繁地改变方向；
- `sigma_t`：消光系数，即一次介质事件发生的总速率；
- `sigma_s / sigma_t`：单次事件后的散射反照率。

光在介质中无碰撞传播距离 `d` 后的透射率由 Beer-Lambert 定律给出：

```text
T_r(d) = exp(-sigma_t * d).
```

RGB 三个通道分别计算指数，因此 `T_r(d)` 是一个 RGB 向量。

## 3. 为什么要选择一个 hero channel

RGB 三个通道的 `sigma_t` 可能不同，但一条几何路径只能选择一个散射距离。本实现先以相同概率 `1/3` 选择通道 `k`，再从该通道的指数分布采样距离：

```text
k = floor(3 * u_channel)
d = -log(1 - u_distance) / sigma_t[k].
```

这只是采样分布，不会把最终结果变成单色。最后使用三个通道混合后的概率密度修正权重，所以 RGB 估计仍然无偏。

若采样距离位于下一个表面之前，即 `d < d_surface`，发生介质散射。混合距离密度为：

```text
p_t(d) = (1/3) * sum_c sigma_t[c] * exp(-sigma_t[c] * d).
```

路径吞吐量乘以碰撞权重：

```text
w_scatter(d) = T_r(d) * sigma_s / p_t(d).
```

`sigma_s` 出现在这里意味着吸收事件已经积分进权重，不需要再随机选择“吸收或散射”。这叫作 survival biasing。

若采样距离超过边界，即 `d >= d_surface`，本段没有碰撞。到达边界的混合概率为：

```text
p_surface(d_surface) = (1/3) * sum_c T_r[c](d_surface).
```

路径吞吐量乘以：

```text
w_surface(d_surface) = T_r(d_surface) / p_surface(d_surface).
```

这两个权重分别对应 `HomogeneousMedium::sampleDistance()` 的两个返回分支。

## 4. 散射方向：HG 相函数

设 `theta` 是新传播方向与旧传播方向的夹角，HG 相函数为：

```text
p_HG(cos(theta)) = (1 - g^2)
                   / (4*pi*(1 + g^2 - 2*g*cos(theta))^(3/2)).
```

- `g = 0`：各向同性散射；
- `g > 0`：偏向前向散射；
- `g < 0`：偏向后向散射。

代码直接按 `p_HG` 采样新方向。因此相函数值与方向 PDF 相除后为 1，路径吞吐量无需额外改变。测试会检查大量采样方向的平均 `cos(theta)` 接近 `g`。

## 5. 光滑边界和介质状态

闭合网格的边界使用普通 Fresnel 介质反射/折射：

```text
以概率 F 反射；
以概率 1 - F 折射。
```

反射时，光仍位于原来的介质中。折射时：

- 从外部折射进入网格：当前介质设为该材质的内部介质；
- 从内部折射离开网格：当前介质清空为空气。

因此，只有折射才会切换介质状态。第一版只支持一个介质层，不支持嵌套介质。

## 6. 介质散射点的 next-event estimation

每次发生真实介质散射后，除了按照 HG 相函数继续随机游走，代码还会独立采样一个光源。对面积光源上的采样点 `y`，令：

```text
wi       = normalize(y - x)
G(x,y)   = abs(dot(n_light, -wi)) / distance(x,y)^2
p_lightA = p(select light) * p_A(y)
p_phaseA = p_HG(wo, wi) * G(x,y)
```

沿阴影线计算介质透射率 `T_shadow` 后，直接光贡献为：

```text
L_NEE = L_e * T_shadow * p_HG(wo, wi) * G(x,y)
        / (p_lightA + p_phaseA).
```

分母是 light sampling 与 phase sampling 的 balance heuristic。环境光使用立体角 PDF，因此没有面积与立体角之间的 `G` 转换：

```text
L_NEE_env = L_e * T_shadow * p_HG(wo, wi)
            / (p_lightW + p_HG(wo, wi)).
```

如果光源位于介质外部，阴影线必须穿过物体边界。当前实现只在 `intIOR == extIOR` 时允许直线穿过边界，因为此时没有折射，连接方向不会改变。边界颜色和介质内的 Beer-Lambert 透射率仍会计入 `T_shadow`。

当 `intIOR != extIOR` 时，代码不会使用忽略折射的有偏近似，而是关闭这条跨边界 NEE 路径。精确支持这种情况需要求解经过折射边界的非直线光源连接。

## 7. 一条路径的执行顺序

```text
1. 找到光线与最近表面的距离 d_surface。
2. 如果当前在介质中，调用 sampleDistance(d_surface)：
   a. 若发生散射，更新吞吐量，执行光源 NEE，再采样 HG 方向并回到第 1 步；
   b. 若没有散射，更新吞吐量并继续处理边界。
3. 如果击中普通表面，执行原来的直接光照和 BSDF 采样。
4. 如果击中介质边界，采样 Fresnel 反射/折射；仅在折射时切换介质。
5. 路径逃出场景、击中光源、被吸收或被 Russian roulette 终止时结束。
```

实现入口在 `RayTracer::pathTrace()`；介质公式在 `HomogeneousMedium`；JSON 参数读取在 `loadInstance()`。

## 8. 当前限制

- 跨越介质边界的 NEE 目前只支持折射率匹配的边界；
- 只支持均匀介质和单层介质状态；
- 网格必须闭合，且三角形法线必须一致朝外；
- 当前相机不做像素内抖动，所以抗锯齿仍沿用项目原来的行为；
- 后续的扩散控制变量应以这个体积结果作为 `F`，不应改变这里的目标分布。

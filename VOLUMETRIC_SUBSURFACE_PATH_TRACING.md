# 均匀介质体积次表面路径追踪实现报告

## 摘要

本项目原本是一个只处理表面相互作用的 CPU 路径追踪器。光线只能在三角形表面发生反射或折射，无法描述光进入牛奶、蜡、皮肤或玉石后，在物体内部传播、吸收、多次散射并从另一个位置离开的现象。

这次实现为渲染器加入了一个基础但完整的**均匀介质体积路径追踪器**。主要工作包括：

- 用吸收系数 `sigma_a`、散射系数 `sigma_s` 和消光系数 `sigma_t` 描述均匀介质；
- 用 Beer-Lambert 定律计算介质透射率；
- 用 RGB hero-channel 方法采样光在介质中的自由传播距离；
- 用 Henyey-Greenstein 相函数采样散射方向；
- 在路径中跟踪“当前是否位于介质内部”；
- 在介质散射点执行 next-event estimation（NEE）；
- 用 multiple importance sampling（MIS）组合光源采样和相函数采样；
- 添加 JSON 材质、Cornell Box 测试场景和数值测试。

本文面向已经理解普通表面路径追踪，但尚未接触体积渲染的学习者。下面会先解释表面路径追踪缺少什么，再逐步推导实现所使用的概念和公式。

## 1. 从表面路径追踪到体积路径追踪

### 1.1 普通表面路径追踪做了什么

普通路径追踪从相机发射一条光线，寻找最近的表面交点。在交点处，它通常会：

1. 计算材质的直接光照；
2. 从 BSDF 采样一个新方向；
3. 更新路径吞吐量 `beta`；
4. 继续追踪下一条光线。

如果一条光线从点 `x` 沿方向 `w` 到达下一个表面，传统真空模型默认这段路上什么都不会发生。光的能量不会衰减，方向也不会改变。

### 1.2 介质中多了两类事件

在参与介质中，光到达下一个表面之前可能发生：

- **吸收**：能量转化为其他形式，路径不再贡献辐射亮度；
- **散射**：光在介质内部改变传播方向，然后继续传播。

因此，进入介质后的每一步都必须先比较两个距离：

```text
d_medium  = 随机采样到的下一次介质事件距离
d_surface = 光线到最近表面的距离
```

如果 `d_medium < d_surface`，先发生介质事件；否则光线无碰撞地到达表面。

这就是表面路径追踪和体积路径追踪最核心的结构差异。

## 2. 均匀介质的物理参数

当前实现使用均匀、非发光的 RGB 介质。均匀表示介质系数不随空间位置变化。

对每个颜色通道定义：

```text
sigma_a：吸收系数
sigma_s：散射系数
sigma_t：消光系数
```

三者关系为：

```text
sigma_t = sigma_a + sigma_s
```

它们的单位是场景长度单位的倒数。例如，如果场景以米为单位，那么系数单位就是 `1/m`。

直观上：

- `sigma_a` 越大，该颜色越容易被吸收；
- `sigma_s` 越大，散射事件越频繁；
- `sigma_t` 越大，光无碰撞传播很远的概率越低；
- `sigma_s / sigma_t` 称为单次散射反照率，表示一次消光事件属于散射而非吸收的比例。

因为本实现对 RGB 分别存储系数，所以介质可以有颜色。例如红色蜡可以让红光传播得更远，同时更快吸收绿光和蓝光。

代码中的数据结构为：

```cpp
class HomogeneousMedium
{
public:
    Colour sigmaA;
    Colour sigmaS;
    float g;
};
```

构造函数会把负系数截断为零，并把相函数参数 `g` 限制在合法范围内。

## 3. Beer-Lambert 定律

### 3.1 透射率

光在消光系数为 `sigma_t` 的均匀介质中传播距离 `d`，途中没有发生任何事件的比例为：

```text
T_r(d) = exp(-sigma_t * d)
```

`T_r` 称为透射率。它也可以理解为“光至少无碰撞传播距离 `d`”的概率。

对 RGB 介质分别计算：

```text
T_r(d) = (
    exp(-sigma_t.r * d),
    exp(-sigma_t.g * d),
    exp(-sigma_t.b * d)
)
```

几个特殊情况：

- `sigma_t = 0` 时，`T_r(d) = 1`，相当于真空；
- `d = 0` 时，`T_r(0) = 1`；
- 距离越长或消光越强，透射率越接近零。

### 3.2 体积渲染方程中的位置

先只考虑一条从介质点到表面的线段。沿该方向返回的亮度可以概念性地写成：

```text
L = T_r(d_surface) * L_surface
  + integral[0, d_surface]
      T_r(t) * sigma_s * L_scatter(t) dt
```

第一项表示光没有发生介质碰撞，直接到达表面；第二项表示光在某个距离 `t` 处发生散射，然后从其他方向获得入射光。

体积路径追踪的任务，就是用 Monte Carlo 方法在“到达表面”和“在某处散射”之间随机选择，并为选择结果计算正确权重。

## 4. 自由程采样

### 4.1 单通道指数分布

对单个颜色通道，无碰撞概率为：

```text
P(D > d) = exp(-sigma_t * d)
```

因此距离随机变量 `D` 的累计分布为：

```text
P(D <= d) = 1 - exp(-sigma_t * d)
```

令均匀随机数 `u` 位于 `[0, 1)`，对累计分布做反函数采样：

```text
u = 1 - exp(-sigma_t * d)
d = -log(1 - u) / sigma_t
```

这就是代码中自由传播距离公式的来源。其概率密度为：

```text
p(d) = sigma_t * exp(-sigma_t * d)
```

### 4.2 为什么 RGB 不能各自采样一个距离

RGB 三个通道可能拥有不同的 `sigma_t`，但一条光线路径只能有一个几何散射点。不能让红色在距离 `d_r` 散射、绿色在 `d_g` 散射，却继续把它们当成同一条路径。

本实现采用 hero-channel 方法：

1. 以相同概率从 R、G、B 中选择一个通道 `k`；
2. 使用该通道的 `sigma_t[k]` 采样一个共享距离；
3. 用三个通道组成的混合 PDF 修正最终 RGB 权重。

```text
k = floor(3 * u_channel)
d = -log(1 - u_distance) / sigma_t[k]
```

这里选择 hero channel 只是在选择采样分布，不代表最终只计算这个颜色。所有颜色仍会通过 RGB 权重参与结果。

### 4.3 混合距离 PDF

因为三个通道各以 `1/3` 的概率被选中，最终距离分布是三个指数分布的混合：

```text
p_t(d) = (1/3) * sum_c [sigma_t[c] * exp(-sigma_t[c] * d)]
```

其中 `c` 遍历 R、G、B。

混合 PDF 很重要：如果采样来自混合分布，却只除以 hero channel 自己的 PDF，其他通道会得到错误期望并产生颜色偏差。

## 5. 两种距离采样结果及其权重

代码中的 `HomogeneousMedium::sampleDistance()` 接收光线到表面的距离 `d_surface`，并返回以下两种情况之一。

### 5.1 情况 A：在表面之前发生散射

如果：

```text
d < d_surface
```

就在：

```text
x_scatter = ray.origin + d * ray.direction
```

创建一个真实的介质散射点。

该事件在体积渲染方程中的被积函数包含：

```text
T_r(d) * sigma_s
```

除以采样距离的混合 PDF 后，路径吞吐量乘以：

```text
w_scatter(d) = T_r(d) * sigma_s / p_t(d)
```

这是 RGB 分量运算：`T_r` 和 `sigma_s` 是颜色，`p_t` 是标量。

代码没有再随机选择“这次事件是吸收还是散射”，而是始终保留散射路径，并把吸收概率包含在 `sigma_s / sigma_t` 所形成的权重中。这种方法称为 **survival biasing**。它不会忽略吸收，只是用连续权重代替了一个可能立刻终止路径的离散随机选择。

### 5.2 情况 B：无碰撞到达表面

如果采样距离大于或等于 `d_surface`，则这一段没有介质碰撞。

在 hero-channel 混合分布下，到达表面的概率为：

```text
p_surface(d_surface)
    = (1/3) * sum_c T_r[c](d_surface)
```

路径吞吐量乘以：

```text
w_surface(d_surface)
    = T_r(d_surface) / p_surface(d_surface)
```

这个权重保证无碰撞分支的期望正好等于 RGB 透射率。之后路径继续处理表面材质。

### 5.3 为什么两个分支都是无偏的

Monte Carlo 估计器的基本形式是：

```text
estimate = integrand / sampling_pdf
```

散射分支用 `T_r * sigma_s / p_t`，无碰撞分支用 `T_r / p_surface`。当对大量随机样本取平均时：

- 无碰撞样本的平均贡献收敛到 `T_r(d_surface)`；
- 碰撞样本的平均贡献收敛到 `integral T_r(t) * sigma_s dt`。

数值测试会分别检查这两个结果，而不只检查程序是否能够运行。

## 6. Henyey-Greenstein 相函数

### 6.1 相函数相当于体积中的 BSDF

表面 BSDF 描述光在表面如何改变方向。介质没有表面法线，因此使用**相函数**描述一次散射前后的方向关系。

设：

```text
forward  = 散射前的传播方向
wi       = 散射后的传播方向
cosTheta = dot(forward, wi)
```

当前实现使用 Henyey-Greenstein（HG）相函数：

```text
p_HG(cosTheta)
    = (1 - g^2)
      / (4*pi*(1 + g^2 - 2*g*cosTheta)^(3/2))
```

参数 `g` 控制方向性：

- `g = 0`：各向同性散射，所有方向概率相同；
- `g > 0`：前向散射，新方向倾向于保持原方向；
- `g < 0`：后向散射，新方向倾向于反向；
- `g` 越接近 `1` 或 `-1`，方向分布越集中。

HG 相函数在整个球面上的积分为 1，因此它本身也是合法的方向 PDF。

### 6.2 方向采样

当 `g` 接近零时，直接均匀采样球面：

```text
cosTheta = 1 - 2*u1
phi      = 2*pi*u2
```

当 `g` 不为零时，使用 HG 分布的反函数采样：

```text
q = (1 - g^2) / (1 - g + 2*g*u1)

cosTheta = (1 + g^2 - q^2) / (2*g)
phi      = 2*pi*u2
```

先在以 `+Z` 为前向的局部坐标系生成方向，再建立以旧传播方向 `forward` 为轴的正交坐标架，将方向转换到世界坐标。

因为方向就是按照 `p_HG` 采样的，所以路径更新中的相函数值与方向 PDF 相消：

```text
phase_value / phase_pdf = p_HG / p_HG = 1
```

因此，相函数随机游走分支不需要额外乘一个颜色权重，但仍需保存 `phase_pdf`，供之后的 MIS 使用。

## 7. 介质边界和路径状态

### 7.1 为什么路径需要记录当前介质

普通表面路径追踪只需要知道当前光线。体积路径追踪还必须知道光线当前位于空气中还是某个介质中。

本实现给 `RayTracer::pathTrace()` 增加了：

```cpp
const HomogeneousMedium* medium
```

- `medium == nullptr`：当前位于空气或真空中；
- `medium != nullptr`：在寻找表面交点后，先对该介质采样自由程。

### 7.2 边界材质

`HomogeneousMediumBSDF` 继承光滑玻璃材质，并额外拥有一个 `HomogeneousMedium`。闭合三角网格既是介质的几何边界，也是 Fresnel 反射/折射表面。

在边界上：

```text
以概率 F     发生反射
以概率 1 - F 发生折射
```

其中 `F` 是 dielectric Fresnel 项。

- 反射不会穿过边界，因此介质状态保持不变；
- 从空气折射进入网格时，`medium` 设为该边界的内部介质；
- 从介质折射离开网格时，`medium` 清空为 `nullptr`。

代码通过入射方向和出射方向相对于几何法线的符号判断是否发生透射：

```text
transmitted = dot(wo, n) * dot(wi, n) < 0
```

因此测试模型必须满足：

- 网格完全闭合；
- 三角形法线一致朝外；
- 光线不会通过裂缝绕过边界。

当前版本只跟踪一个介质指针，不支持嵌套介质或重叠介质。

## 8. 介质散射点的直接光照

### 8.1 为什么需要 next-event estimation

只使用 HG 相函数随机游走也能最终击中光源，因此理论上可以得到正确结果。但面积光源通常只占很小的立体角，随机方向直接命中光源的概率很低，会产生大量高亮离群样本和严重噪声。

Next-event estimation 在每个介质散射点主动采样一个光源：

1. 随机选择一个光源；
2. 在面积光源上采样一点，或从环境光采样一个方向；
3. 从散射点发射阴影线；
4. 检查遮挡并计算介质透射率；
5. 计算该方向上的 HG 相函数值；
6. 用 MIS 与相函数采样组合。

这样即使光源很小，每个散射点也有机会直接获得光照。

### 8.2 介质阴影线

设介质散射点为 `x`，光源采样点为 `y`，方向和距离为：

```text
wi = normalize(y - x)
r  = length(y - x)
```

如果光源也位于同一介质内，并且中间没有表面，阴影权重为：

```text
T_shadow = T_r(r)
```

在测试场景中，光源位于介质物体外部。阴影线必须先传播到介质边界，再在空气中传播到光源。当前实现只允许阴影线直线穿过**折射率匹配边界**：

```text
intIOR == extIOR
```

此时 Snell 定律不会改变传播方向，Fresnel 反射率也为零，所以直线连接是精确的。阴影权重包括介质内透射率和边界颜色：

```text
T_shadow = T_r(distance_to_boundary) * boundary_transmission
```

离开边界后还会继续检查边界到光源之间是否被其他几何体遮挡。

如果 `intIOR != extIOR`，连接光源的路径应在边界处折射。简单地让阴影线直线穿过会产生偏差，因此代码会拒绝该 NEE 连接。相函数随机游走仍然可以正常穿过这种边界，只是噪声会更高。精确解决折射边界 NEE 需要寻找满足 Snell 定律的边界连接点，不属于当前基础实现的范围。

## 9. 面积光源的 NEE 与 MIS

### 9.1 光源采样贡献

对面积光源上的采样点 `y`，定义几何项：

```text
G(x, y) = abs(dot(n_light, -wi)) / distance(x, y)^2
```

注意介质散射点没有表面法线，因此几何项只有光源端的余弦，没有散射点端的余弦。

光源策略使用面积 PDF：

```text
p_light_A = p(select_light) * p_A(y)
```

相函数策略的 PDF 原本是立体角 PDF。要与面积 PDF 相加，必须先转换到同一种测度：

```text
p_phase_A = p_HG(forward, wi) * G(x, y)
```

如果一个 PDF 按面积计量、另一个按立体角计量，直接相加没有数学意义。这是实现 MIS 时很容易犯的错误。

### 9.2 Balance heuristic

使用 balance heuristic 后，光源采样得到的直接光贡献可以写成：

```text
L_NEE
    = L_e(y -> x)
      * T_shadow
      * p_HG(forward, wi)
      * G(x, y)
      / (p_light_A + p_phase_A)
```

它等价于普通形式：

```text
L_NEE = integrand / p_light_A * w_light

w_light = p_light_A / (p_light_A + p_phase_A)
```

合并后，`p_light_A` 被约掉，得到代码中的分母形式。

### 9.3 相函数采样击中光源

路径还会按照 HG 相函数继续随机游走。如果该方向最终击中光源，就使用另一个 MIS 权重：

```text
w_phase = p_phase_A / (p_light_A + p_phase_A)
```

为了正确计算这个权重，代码必须保存：

- 上一个非 delta 散射点的位置；
- 当时的方向 PDF `prevPdfw`。

如果路径从介质散射点穿过折射率匹配的 delta 边界，方向没有改变。此时不能在边界处清除 PDF 和起点，否则到达光源时会使用错误距离计算面积 PDF。实现会让这份 MIS 状态穿过匹配边界继续传播。

如果边界会产生真实折射，当前 NEE 策略无法生成同一条弯曲路径，因此不应与它竞争。代码会在这种边界处清除该 MIS 状态，使相函数路径的光源贡献保持完整权重。

## 10. 环境光 NEE

环境光采样直接提供方向和立体角 PDF，因此不需要面积转换：

```text
p_light_W = p(select_light) * p_environment(wi)
p_phase_W = p_HG(forward, wi)
```

直接光贡献为：

```text
L_NEE_environment
    = L_e(wi)
      * T_shadow
      * p_HG(forward, wi)
      / (p_light_W + p_phase_W)
```

阴影线离开折射率匹配边界后，会继续向无穷远检查场景遮挡。

## 11. 完整路径算法

下面的伪代码对应当前 `RayTracer::pathTrace()` 的执行顺序：

```text
function pathTrace(ray, beta, medium, previousMISState):
    surfaceHit = intersectScene(ray)

    if medium exists and surfaceHit exists:
        scattered, distance, weight =
            medium.sampleDistance(surfaceHit.distance)
        beta *= weight

        if scattered:
            x = ray.at(distance)

            # 主动连接一个光源
            L = beta * computeMediumDirect(x, ray.direction, medium)

            # 对较长路径执行 Russian roulette
            if roulette terminates:
                return L

            # 按 HG 相函数继续随机游走
            wi, phasePdf = medium.samplePhase(ray.direction)
            nextRay = Ray(x + epsilon * wi, wi)
            return L + pathTrace(
                nextRay,
                beta,
                medium,
                MISState(x, phasePdf))

    if no surfaceHit:
        return beta * environmentEmissionWithMIS

    if surface is light:
        return beta * emittedRadianceWithMIS

    L = beta * surfaceDirectLighting
    wi, bsdfValue, pdf = sampleSurfaceBSDF()
    beta *= bsdfValue * abs(dot(wi, normal)) / pdf

    if surface is a medium boundary and sample is transmission:
        toggle medium state

    update or preserve MIS state
    return L + pathTrace(nextRay, beta, medium, MISState)
```

可以看到，体积路径追踪没有替换原来的表面路径追踪。它是在每次处理表面之前，多了一次介质距离采样；发生介质散射时，路径在空间内部创建新顶点并继续递归。

## 12. 场景材质配置

场景加载器新增了：

```json
{
  "bsdf": "homogeneous_medium",
  "reflectance": "1_1_1.png",
  "sigmaA": "0.15 0.80 1.50",
  "sigmaS": "6.0 6.0 6.0",
  "g": "0.0",
  "intIOR": "1.0",
  "extIOR": "1.0"
}
```

参数含义：

- `reflectance`：边界透射颜色，测试场景使用白色；
- `sigmaA`：RGB 吸收系数；
- `sigmaS`：RGB 散射系数；
- `g`：HG 相函数的各向异性参数；
- `intIOR`：物体内部折射率；
- `extIOR`：物体外部折射率。

测试场景将前方较矮的盒子设为均匀介质，后方较高的盒子仍为普通漫反射材质。为了让跨边界直线 NEE 严格成立，测试介质使用：

```text
intIOR = extIOR = 1.0
```

需要特别注意：系数与模型尺寸共同决定光学厚度。把模型放大两倍但保持 `sigma_a` 和 `sigma_s` 不变，会让典型路径经历更多散射和吸收。

## 13. 代码结构对应

### `RTAssignment/Materials.h`

- `HomogeneousMedium::sigmaT()`：计算 `sigma_a + sigma_s`；
- `HomogeneousMedium::transmittance()`：实现 Beer-Lambert 定律；
- `HomogeneousMedium::sampleDistance()`：hero-channel 自由程采样及两种权重；
- `HomogeneousMedium::phase()`：计算 HG 相函数；
- `HomogeneousMedium::samplePhase()`：采样 HG 方向；
- `HomogeneousMediumBSDF`：把光滑边界与内部介质组合起来；
- `supportsStraightTransmission()`：只有折射率严格匹配时才允许直线穿界 NEE。

### `RTAssignment/Renderer.h`

- `mediumShadowTransmittance()`：追踪介质阴影线、计算透射率和遮挡；
- `computeMediumDirect()`：采样面积光源或环境光，并计算 NEE/MIS；
- `pathTrace()`：在表面之前采样介质事件、更新介质状态并保存 MIS 状态。

### `RTAssignment/SceneLoader.h`

- 读取 `homogeneous_medium`、`sigmaA`、`sigmaS`、`g` 和 IOR 参数；
- 创建 `HomogeneousMediumBSDF`。

### `RTAssignment/VolumeTests.cpp`

- 检查真空不会产生介质碰撞；
- 检查无碰撞估计收敛到 Beer-Lambert 透射率；
- 检查碰撞估计收敛到解析散射积分；
- 检查 HG 样本的平均余弦收敛到参数 `g`；
- 检查只有折射率匹配边界才启用直线 NEE。

## 14. 验证结果

实现完成后进行了三类验证：

1. **编译验证**：macOS CMake 工程和主渲染器成功编译；
2. **数值验证**：`VolumeTests` 中的所有 Monte Carlo 统计测试通过；
3. **图像验证**：对 `volumetric-cornell` 进行实际渲染，加入 NEE 后，体积盒子中由偶然击中光源造成的高亮离群噪声明显减少。

运行测试：

```bash
ctest --test-dir build --output-on-failure
```

运行体积场景：

```bash
./build/RTAssignment.app/Contents/MacOS/RTAssignment \
  -scene volumetric-cornell \
  -SPP 64 \
  -outputFilename volumetric.hdr
```

## 15. 当前限制和后续方向

当前实现是研究项目的基础体积目标估计器，仍有以下限制：

- 只支持空间中系数不变的均匀介质；
- 只支持单层介质状态，不支持嵌套或重叠介质；
- 跨边界 NEE 只支持 `intIOR == extIOR` 的直线连接；
- 折射率不匹配时仍可随机游走，但没有精确的折射边界 NEE；
- 没有 Dwivedi sampling、路径引导或其他高级随机游走重要性采样；
- 没有异质、分层、光谱或荧光介质；
- 网格必须闭合且法线方向正确。

在这些限制下，当前结果仍然提供了一个清晰的普通体积次表面路径追踪基线。后续扩散 BSSRDF 控制变量应以这个估计器作为昂贵目标 `F`，通过降低方差加速它，而不能改变它所代表的体积传输目标。

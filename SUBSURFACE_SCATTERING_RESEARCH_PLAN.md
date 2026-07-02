# Research Plan: Diffusion as a Control Variate for Volumetric Subsurface Path Tracing

## 1. Working title

**Reducing Variance in Volumetric Subsurface Path Tracing with a Diffusion Control Variate**

## 2. Core idea

The research target is an unbiased volumetric path-traced subsurface-scattering result. A traditional diffusion/BSSRDF method is not treated as a competing final renderer. It is used as a **Monte Carlo control variate**.

Let:

- `F(u)` be one expensive volumetric subsurface path-tracing sample;
- `C(u)` be one cheap traditional diffusion/BSSRDF sample;
- `u` be the shared primary random sample used to generate a correlated pair;
- `L_F = E[F]` be the desired volumetric result;
- `L_C = E[C]` be the diffusion result.

If `L_C` were known exactly, the control-variate estimator would be

```text
L_cv = beta * L_C + (1/N) * sum_i(F(u_i) - beta * C(u_i)).
```

For `beta = 1`, this is the intuitive decomposition

```text
volumetric result = traditional result + residual(volumetric - traditional).
```

The subtraction does not reduce variance merely because the two rendered images look similar. `F(u_i)` and `C(u_i)` must be **positively correlated sample by sample**. The project must therefore design and measure a coupling between the two estimators.

In a general scene, `L_C` is not analytically known. Estimate it cheaply with many diffusion samples:

```text
L_cv = beta * mean_M(C(v_j))
     + mean_N(F(u_i) - beta * C(u_i)),
```

where the `v_j` control-only samples are independent of the `u_i` paired residual samples. This is a two-level Monte Carlo/control-variate estimator. Choose `M` much larger than `N` when diffusion samples are cheap.

## 3. Main research question

**Can a classical diffusion approximation, coupled to a volumetric random-walk estimator through common random numbers, reduce the variance of unbiased volumetric subsurface path tracing at equal computation time?**

### Secondary questions

- Which coupling produces the strongest covariance between the volumetric and diffusion samples?
- Is the natural coefficient `beta = 1` effective, or is a fitted coefficient better?
- How should samples be allocated between the cheap control estimate (`M`) and the expensive paired residual (`N`)?
- Under which material and geometric conditions does the diffusion control stop being useful?
- Does a control variate help per RGB channel equally, or are channel-wise coefficients needed?

### Hypotheses

- **H1:** In optically thick, highly scattering media, diffusion and volumetric samples will be strongly correlated, making the residual less variable than `F`.
- **H2:** The estimator will reduce equal-time MSE when the variance saved by the correlation exceeds the cost of computing `C` and its mean.
- **H3:** Correlation will weaken for thin objects, low-albedo media, strong anisotropy, and boundary-dominated transport.
- **H4:** A pilot-estimated `beta` and cost-aware allocation of `M` and `N` will outperform the fixed choice `beta = 1`.

## 4. Mathematical basis

For paired random variables `F` and `C`, with an exact control mean, define

```text
Y_beta = F - beta * (C - L_C).
```

Its expectation is unchanged:

```text
E[Y_beta] = E[F] = L_F.
```

Its variance is

```text
Var[Y_beta]
  = Var[F] + beta^2 Var[C] - 2 beta Cov[F,C].
```

The variance-minimizing scalar coefficient is

```text
beta* = Cov[F,C] / Var[C].
```

With correlation `rho`, the ideal variance at `beta*` is

```text
Var[Y_beta*] = Var[F] * (1 - rho^2).
```

This makes squared correlation the central diagnostic. A visually similar diffusion image is not enough; the paired estimator must exhibit high measured `rho`.

When the control mean is estimated from `M` independent cheap samples, the variance is

```text
Var[L_cv]
  = beta^2 Var[C] / M
  + Var[F - beta C] / N.
```

The paper must include both terms and count the cost of both levels. It must not present the diffusion image as free.

For RGB rendering, begin with independent coefficients `beta_r`, `beta_g`, and `beta_b`. A luminance-only coefficient is simpler but may fail when extinction is strongly chromatic.

## 5. What is being compared

The primary comparison is between two unbiased estimators of the **same volumetric target**:

1. **Baseline:** ordinary volumetric subsurface path tracing, `mean(F)`.
2. **Proposed:** diffusion control variate plus paired residual, `beta * mean(C) + mean(F - beta C)`.

The raw diffusion renderer is included only to show the approximation and to construct the control. Its bias relative to the volumetric target is not inherited by the final estimator because the residual restores the difference in expectation.

Required ablations:

- uncorrelated `F` and `C` samples;
- common-random-number coupling;
- improved coupling, if developed;
- `beta = 1`;
- pilot-fitted `beta`;
- exact/high-quality control mean versus finite-`M` control mean;
- standard volumetric path tracing at equal SPP and equal time.

## 6. Scope

### Core scope

- steady-state RGB rendering;
- one homogeneous participating medium inside a closed object;
- absorption `sigma_a`, scattering `sigma_s`, extinction `sigma_t`, and anisotropy `g`;
- a shared smooth dielectric boundary;
- Henyey–Greenstein phase sampling for volumetric random walks;
- a classical dipole diffusion BSSRDF as the initial traditional control;
- camera paths whose subsurface segment is isolated for control-variate treatment;
- unbiased estimation, covariance measurement, and equal-time evaluation.

### Deferred work

- heterogeneous, layered, fluorescent, or spectral media;
- rough boundaries;
- multiple nested media;
- neural control variates;
- advanced random-walk guidance such as Dwivedi sampling;
- denoising;
- reuse across animation frames.

These can obscure whether the basic diffusion control variate works.

## 7. Current project baseline

The repository already contains a CPU surface path tracer with MIS, Russian roulette, triangle/BVH traversal, JSON scenes, area/environment lights, multithreaded rendering, and HDR output.

Missing components are:

- a medium and medium state on paths;
- free-flight sampling and a phase function;
- reliable entry/exit boundary tracking;
- a diffusion BSSRDF;
- paired primary-sample streams;
- separate film accumulators for `F`, `C`, and `F - beta C`;
- per-run covariance and cost statistics;
- a reproducible batch experiment mode.

Before research measurements:

- replace per-worker RNG use with deterministic streams derived from `(seed, pixel, sample, dimension/stream)`;
- gate the current unconditional generation of 1,024 VPLs in `render()`, which would contaminate timings;
- avoid the unfinished rough dielectric implementation and use one tested smooth dielectric boundary;
- separate setup/precomputation time from sampling time and report both.

## 8. Estimator architecture

### 8.1 Separate the controlled contribution

Decompose pixel radiance conceptually as

```text
L = L_nonSSS + L_SSS.
```

Apply the control variate only to `L_SSS`. Surface reflection, camera sampling, and unrelated indirect-light noise should not swamp the covariance measurement. The final image combines the conventional estimate of `L_nonSSS` with the controlled estimate of `L_SSS`.

### 8.2 Three sampling modes

Implement these modes using the same renderer:

1. `volume`: generate `F(u)` only;
2. `control`: generate cheap `C(v)` samples for the high-sample control mean;
3. `paired-residual`: generate both `F(u)` and `C(u)` from a shared sample record and accumulate `F - beta C`.

The final controlled pixel estimate is assembled from the control and residual films.

### 8.3 Sample records

Do not let each algorithm consume a mutable RNG sequentially, because different path lengths immediately desynchronize the streams. Use named or indexed random dimensions, for example:

- camera jitter/lens;
- boundary Fresnel choice;
- light selection and light position;
- surface-scattering choices;
- subsurface entry variables;
- radial/azimuthal exit variables for diffusion;
- free-flight and phase variables per random-walk event.

A `SampleRecord` or counter-based sampler should return a repeatable value for `(pixel, sample, stream, event, dimension)`. Both methods read the dimensions that have corresponding meaning.

### 8.4 Initial coupling: common random numbers

The minimum viable coupling uses the same random values for common decisions:

- identical camera ray and first surface hit;
- identical Fresnel reflection/transmission decision at entry;
- identical light choice and light-sample dimensions;
- shared azimuthal variables where both methods use a radial/exit construction;
- shared outer surface-path decisions before and after the subsurface segment where possible.

Run `F(u)` and `C(u)` independently after a decision has no meaningful correspondence, but retain indexed streams so later common decisions can realign.

### 8.5 Improved coupling research

The likely technical contribution is a mapping that correlates diffusion displacement with random-walk transport more strongly than naïve shared RNG values.

Candidate progression:

1. **Naïve primary-space coupling:** reuse the same uniforms without semantic mapping.
2. **Quantile/rank coupling:** use the same uniform quantile for diffusion exit radius and a random-walk summary such as exit displacement or transport depth.
3. **Endpoint coupling:** condition or map the diffusion sample using the volumetric path’s entry/exit side, radius, and azimuth while preserving each estimator’s marginal distribution.
4. **Stratified coupling by event class:** maintain separate controls for reflected exit, transmitted exit, or scattering-order bands.

Any improved mapping must preserve the correct marginal distribution of both estimators. A mapping that merely makes samples look similar but changes either expectation creates bias.

Begin with the naïve valid coupling. Treat improved coupling as a measured extension, not a prerequisite for the first correct result.

### 8.6 Estimating the control mean

Use an independent sample set for `mean_M(C)` so the estimator and variance analysis remain easy to audit. Since diffusion is cheap, render it at a larger `M` until its variance contribution is small but still included in total cost.

Also test a practically converged cached control image. Label this clearly as the “precomputed-control” scenario and amortize or separately report its precomputation cost.

### 8.7 Choosing `beta`

Evaluate:

- fixed `beta = 1`;
- per-channel global `beta`;
- per-pixel or region-wise `beta` only as a later extension.

Estimate `beta` from an independent pilot set or use data splitting/cross-fitting. Estimating and applying a noisy coefficient on the same small sample set can introduce bias or unstable results. Clamp/regularize coefficients only with a declared rule derived before final evaluation.

## 9. Physical implementations

### Volumetric target `F`

- homogeneous RGB coefficients;
- spectral/hero-channel free-flight sampling with correct weights;
- Henyey–Greenstein phase sampling, beginning with `g = 0`;
- shared smooth dielectric entry and exit;
- next-event estimation/transmittance where supported;
- unbiased Russian roulette;
- a high safety event cap whose activation rate is reported.

### Traditional control `C`

- standard dipole diffusion BSSRDF;
- the same `sigma_a`, `sigma_s`, `g`, IOR, geometry, camera, and lighting inputs;
- reduced coefficients computed in one shared utility;
- Fresnel boundary correction;
- sampled exit point and direct-light connection;
- correct sampling PDF and throughput.

The control is allowed to be physically inaccurate. Its requirements are that its own estimator be unbiased for the chosen diffusion approximation, cheap, and correlated with `F`.

## 10. Verification

### Component tests

- exponential free-flight mean/distribution;
- Henyey–Greenstein normalization and measured mean cosine;
- dielectric Fresnel event frequencies;
- Beer–Lambert attenuation in an absorbing slab;
- diffusion `sample/PDF/evaluate` consistency;
- reproducibility across runs and thread counts;
- finite/nonnegative values and event-cap audit.

### Control-variate tests

Use a scalar synthetic test before rendering:

- select two known correlated functions `F(u)` and `C(u)`;
- verify that ordinary and controlled sample means agree within confidence intervals;
- verify the empirical variance formula and fitted `beta`;
- verify the extra `Var[C]/M` term when the control mean is estimated.

Then test a single pixel and a small image:

- record every paired `(F_i, C_i)`;
- compute sample covariance, correlation, predicted controlled variance, and observed variance over independent runs;
- check that the controlled and high-SPP volumetric means agree statistically;
- deliberately shuffle `C_i` to destroy correlation and confirm that the benefit disappears.

## 11. Experimental design

### 11.1 Primary factor

Estimator:

- ordinary volumetric path tracing;
- diffusion-controlled volumetric path tracing.

### 11.2 Physical factor matrix

| Factor | Suggested levels | Expected control behavior |
|---|---:|---|
| Optical thickness `d * sigma_t'` | 0.25, 1, 4, 16 | correlation should improve toward diffusion regime |
| Reduced albedo | 0.5, 0.8, 0.95, 0.99 | higher values should favor diffusion |
| Anisotropy `g` | 0.0, 0.5, 0.8 | tests reduced-scattering mapping |
| Shape | slab, sphere, thin curved object | flat/thick to boundary-dominated |
| Illumination | broad and localized area lights | smooth versus high-frequency response |

Use one-factor sweeps first, then select a small set of interaction cases. The purpose is to map control-variate usefulness, not to exhaust every material combination.

### 11.3 Scene progression

1. **Single pixel on a slab:** estimator/covariance debugging.
2. **Small slab image:** radial-profile and spatial-correlation study.
3. **Sphere:** curvature and symmetry.
4. **Thin curved object:** expected weak-control/failure case.
5. **Cornell-box scene:** final whole-renderer demonstration.

### 11.4 Budgets and replicates

- use at least 10 independent seeds for main conditions if feasible;
- compare equal volumetric sample count to expose pure variance reduction;
- compare equal total wall-clock time for the practical result;
- include control precomputation and residual cost in the main timing;
- separately report an amortized cached-control scenario;
- determine `M/N` from pilot variance and measured sample costs, then freeze it for final runs;
- use optimized builds and fixed hardware/resolution.

## 12. Metrics

### Central control-variate metrics

- covariance `Cov[F,C]` and correlation `rho`;
- fitted `beta` per channel;
- residual variance `Var[F - beta C]`;
- variance-reduction factor `Var[F] / Var[F - beta C]` with exact control mean;
- full estimator variance including finite `M`;
- efficiency `1 / (MSE * time)`;
- break-even time including control computation;
- optimal/used `M:N` allocation.

### Image metrics

- linear-HDR MSE/RMSE against a converged volumetric reference;
- bias check from repeated-run mean images;
- variance maps and residual images;
- RMSE versus time and versus number of expensive volume samples;
- identical exposure/tonemapping for displayed comparisons.

Do not use the diffusion image’s error as the proposed estimator’s error. The proposed estimator targets the volumetric solution.

## 13. Analysis criteria

The method is successful only if:

1. the controlled mean is statistically consistent with the volumetric reference;
2. observed variance matches the covariance-based prediction;
3. equal-time MSE is lower after counting both control and residual work;
4. the benefit repeats across independent seeds;
5. failure regimes are identified rather than hidden.

Negative covariance or very small `rho` is a valid result. With a fitted `beta`, the method should naturally reduce the control’s influence; however, its computation can still make efficiency worse.

## 14. Implementation phases

### Phase 0 — Estimator prototype (2–3 days)

- implement the scalar correlated-function test;
- verify the two-level estimator, variance formula, and independent pilot fit of `beta`;
- define stored statistics and experiment metadata.

**Exit:** predicted and measured variance agree on a known problem.

### Phase 1 — Reproducible renderer foundation (3–5 days)

- add headless method selection and batch output;
- implement deterministic indexed sample streams;
- add separate `F`, `C`, residual, covariance, timing, and event accumulators;
- gate unrelated VPL work.

**Exit:** results are identical across repeated runs and thread counts.

### Phase 2 — Shared medium and boundary (1 week)

- add coefficients, phase function, medium state, free-flight sampling, and smooth dielectric boundaries;
- pass Beer–Lambert, phase, and Fresnel tests.

**Exit:** shared physical components are validated.

### Phase 3 — Volumetric target estimator (1–2 weeks)

- implement homogeneous subsurface random walk;
- validate on slab and sphere;
- produce a converged small reference.

**Exit:** ordinary volumetric results are stable and statistically validated.

### Phase 4 — Diffusion control estimator (1–2 weeks)

- implement the dipole BSSRDF from the same physical inputs;
- validate its sample/PDF pair and radial profile;
- measure cost per control sample.

**Exit:** the diffusion estimator is correct for its approximate model.

### Phase 5 — Paired estimator and coupling (1–2 weeks)

- implement naïve common-random-number pairing;
- measure covariance at sample, pixel, and image levels;
- add independent control-mean rendering and pilot-fitted `beta`;
- attempt one improved coupling only after the baseline is correct.

**Exit:** controlled and ordinary estimators agree in mean, with reproducible variance measurements.

### Phase 6 — Pilot, allocation, and final experiment lock (3–5 days)

- measure costs and variance components;
- choose `M/N`, seed count, resolutions, and physical cases;
- freeze the estimator and experiment manifest.

**Exit:** every final render is specified without manual code edits.

### Phase 7 — Final runs and paper (2–3 weeks)

- render all replicates and references;
- generate covariance, variance, MSE-time, and failure-regime plots;
- write the paper and perform a reproducibility audit.

Estimated core duration: **8–11 weeks**.

## 15. Paper structure

1. **Abstract** — control-variate idea, unbiasedness, principal variance/efficiency result.
2. **Introduction** — random-walk robustness and noise; diffusion as a cheap correlated approximation.
3. **Related work** — diffusion BSSRDFs, volumetric subsurface path tracing, control variates and residual rendering.
4. **Background** — subsurface transport, diffusion approximation, control-variate theory.
5. **Method** — two-level estimator, common random numbers, coupling, coefficient and allocation.
6. **Implementation** — shared physical model and integration into this renderer.
7. **Validation** — component and unbiasedness tests.
8. **Experiments** — scenes, parameters, budgets, reference and replicate protocol.
9. **Results** — covariance, variance reduction, equal-time MSE, ablations, failure cases.
10. **Discussion** — why correlation succeeds/fails, overhead, limitations and extensions.
11. **Conclusion** — whether and where diffusion is an effective control variate.

## 16. Planned figures and tables

- estimator diagram: high-sample cheap control plus low-sample paired residual;
- scatter plots of paired `F_i` versus `C_i` with `rho` and fitted `beta`;
- volumetric, diffusion, residual, and reconstructed controlled images;
- variance maps for ordinary and controlled estimators;
- RMSE versus total time;
- variance-reduction factor versus optical thickness/albedo;
- ablation table for uncorrelated, naïve coupled, and improved coupled sampling;
- cost/variance table showing `M`, `N`, and both variance terms;
- thin-object failure case.

## 17. Literature anchors

Bibliographic details should be checked before final citation. Start with:

- Jensen et al., the practical dipole/BSSRDF formulation;
- Chiang, Kutz, and Burley, *Practical and Controllable Subsurface Scattering for Production Path Tracing*;
- Rousselle, Jarosz, and Novák, *Image-Space Control Variates for Rendering*;
- general multifidelity/two-level Monte Carlo control-variate literature;
- correlated and residual path-tracing literature;
- work on unbiased path-traced subsurface next-event estimation and random-walk guidance.

For each control-variate source, extract the exact estimator, assumptions about a known/estimated control mean, covariance estimation, coefficient fitting, sample allocation, and how all costs are counted.

## 18. Main risks

| Risk | Mitigation |
|---|---|
| Diffusion and volume images are similar but samples are weakly correlated | Measure pairwise `rho` immediately; focus research on valid coupling rather than image similarity. |
| Sequential RNG consumption destroys coupling | Use indexed/named random dimensions. |
| Estimated control mean adds substantial noise | Include its variance term; increase `M`; optimize allocation using measured cost and variance. |
| Fitted `beta` overfits or biases results | Fit on independent pilot data or cross-fit; freeze before final trials. |
| Coupling changes a marginal distribution | Test each estimator alone against its uncoupled implementation and analytic/statistical checks. |
| Control overhead exceeds variance reduction | Report equal-time efficiency and break-even cost, including precomputation. |
| Non-SSS noise hides covariance | Isolate the subsurface contribution and control only that term. |
| Diffusion fails on thin geometry | Treat it as an expected low-correlation regime; the final estimator remains unbiased but may not be faster. |

## 19. Minimum viable contribution

- a validated homogeneous volumetric subsurface estimator `F`;
- a validated dipole estimator `C` using the same material inputs;
- deterministic common-random-number pairing;
- an unbiased two-level estimator with finite-`M` control mean;
- independent pilot fitting of per-channel `beta`;
- measured covariance, variance reduction, and equal-time efficiency;
- at least one strong-control and one weak-control physical regime;
- reproducible raw HDR outputs and metadata.

## 20. Immediate next actions

1. Build the scalar control-variate prototype and verify the equations empirically.
2. Decide the exact diffusion model (standard dipole is the initial recommendation).
3. Define the paired sample record and named random streams before writing either subsurface integrator.
4. Add separate accumulation of `F`, `C`, `F - beta C`, `F*C`, `F^2`, and `C^2`.
5. Implement and validate the volumetric and diffusion estimators independently.
6. Measure naïve coupling correlation on a single slab pixel before building complex scenes.

The governing principle is: **the traditional method reduces variance only through a cheap mean and strong sample-wise covariance with the volumetric estimator; the residual preserves the volumetric expectation.**

The literature supports your idea, but I did not find a paper directly applying a diffusion BSSRDF as a sample-wise control variate for volumetric subsurface random walks. That appears to be the potential research gap—subject to a more exhaustive scholarly search.

## Most relevant papers

1. **Yang & Moon, “Imperfect Image-Space Control Variates for Monte Carlo Rendering,” 2025.**
   Probably the closest mathematical reference because it handles control variates whose expectations are unknown and estimated using additional noisy samples—exactly your finite-\(M\) diffusion-control problem. Their control is spatial rather than diffusion-based. [Paper and code](https://cglab.gist.ac.kr/siga25iicv/)

2. **Rousselle, Jarosz & Novák, “Image-Space Control Variates for Rendering,” 2016.**
   Establishes control-variate rendering using covariance and relates residual rendering to control-variate estimators. Useful for deriving your estimator and experimental methodology. [Paper](https://jannovak.info/publications/CVRendering/index.html)

3. **Fan et al., “Optimizing Control Variate Estimators for Rendering,” 2006.**
   Studies automatically optimized coefficients for generic correlated rendering controls. Relevant to estimating \(\beta=\operatorname{Cov}(F,C)/\operatorname{Var}(C)\). [Eurographics record](https://diglib.eg.org/items/dac6b656-4281-40e4-bd5b-0e405b169973)

4. **Vidal-Codina et al., “Efficient Control Variates for Uncertainty Quantification of Radiation Transport,” 2017.**
   The closest conceptual precedent: it explicitly uses diffusion approximations as low-fidelity control variates for higher-fidelity radiation transport. Its target is uncertainty quantification rather than image-space photon-path estimation, so your application and coupling problem remain different. [Paper](https://www.sciencedirect.com/science/article/abs/pii/S0022407316304599)

5. **Gorodetsky et al., “A Generalized Approximate Control Variate Framework for Multifidelity Uncertainty Quantification,” 2020.**
   Provides theory for approximate control means and optimal allocation across expensive and cheap models. Useful for deriving the \(M:N\) diffusion/volume sample allocation. [Paper](https://doi.org/10.1016/j.jcp.2020.109257)

## Diffusion controls

6. **Jensen et al., “A Practical Model for Subsurface Light Transport,” 2001.**
   The canonical dipole diffusion BSSRDF. This is the cleanest first control because it is established, inexpensive, and parameterized through physical scattering coefficients. [Canonical paper page](https://graphics.stanford.edu/~srm/publications/SG01-subsurf-abstract.html)

7. **Habel, Christensen & Jarosz, “Photon Beam Diffusion,” 2013.**
   A more accurate diffusion approximation evaluated with only a few Monte Carlo samples. This could become a stronger—but more expensive—control-variate ablation. [Paper and supplementary code](https://graphics.pixar.com/library/PhotonBeamDiffusion/)

8. **Christensen & Burley, “Approximate Reflectance Profiles for Efficient Subsurface Scattering,” 2015.**
   Presents very cheap fitted BSSRDF profiles designed to match brute-force Monte Carlo references. Potentially an excellent control if correlation matters more than physical derivation. [Pixar publication](https://graphics.pixar.com/library/ApproxBSSRDF/)

9. **Frisvad, Hachisuka & Kjeldsen, “Directional Dipole Model for Subsurface Scattering,” 2014.**
   Improves directional behavior over the classical point-source dipole. Useful if illumination direction strongly affects correlation. [Paper](https://cs.uwaterloo.ca/~thachisu/dirpole.pdf)

## Volumetric target and competing variance reduction

10. **Chiang, Kutz & Burley, “Practical and Controllable Subsurface Scattering for Production Path Tracing,” 2016.**
    A practical random-walk subsurface target, including parameter conversion and chromatic distance sampling. [Disney publication](https://disneyanimation.com/publications/practical-and-controllable-subsurface-scattering-for-production-path-tracing/)

11. **Wrenninge, Villemin & Hery, “Path Traced Subsurface Scattering Using Anisotropic Phase Functions and Non-Exponential Free Flights,” 2017.**
    Covers production random walks, anisotropy, scattering-order distinctions, and Dwivedi-style guidance. [Pixar paper](https://graphics.pixar.com/library/PathTracedSubsurface/)

12. **Křivánek & d’Eon, “A Zero-Variance-Based Sampling Scheme for Monte Carlo Subsurface Scattering,” 2014.**
    The most important existing variance-reduction baseline. It guides random walks toward the surface using zero-variance theory. Your method should be positioned as control-variate variance reduction rather than path-guiding variance reduction. [Author’s paper page](https://cgg.mff.cuni.cz/~jaroslav/papers/2014-zerovar/)

13. **Koerner et al., “Subdivision Next-Event Estimation for Path-Traced Subsurface Scattering,” 2016.**
    An unbiased method addressing difficult light connections through refractive boundaries. Relevant as another orthogonal source of variance reduction. [Eurographics paper](https://diglib.eg.org/items/fdf0c3c5-93d5-4b9e-9c15-4080a19220d9)

14. **Xu et al., “Residual Path Integrals for Re-rendering,” 2024.**
    Relevant to constructing correlated path differences with common random numbers, although its application is scene editing rather than diffusion-versus-volume transport. [Project page](https://www.iliyan.com/publications/ResidualPathIntegral)

## Suggested paper positioning

A defensible related-work narrative would be:

1. Diffusion models provide cheap approximate subsurface transport.
2. Random-walk path tracing removes diffusion assumptions but converges slowly.
3. Existing work reduces random-walk variance through importance sampling, Dwivedi guidance, and specialized NEE.
4. Rendering control-variate work exploits correlated approximate estimators, including imperfect controls.
5. **Your proposed contribution:** use a diffusion BSSRDF as a low-fidelity, sample-correlated control variate for an unbiased volumetric subsurface target, including coupling, noisy-control-mean estimation, and cost-optimal sample allocation.

The five papers I would read first are Yang–Moon 2025, Rousselle et al. 2016, Vidal-Codina et al. 2017, Jensen et al. 2001, and Křivánek–d’Eon 2014.

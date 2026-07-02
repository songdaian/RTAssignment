#pragma once

#include "Core.h"
#include "Imaging.h"
#include "Sampling.h"

#pragma warning( disable : 4244)
#pragma warning( disable : 4305) // Double to float

class BSDF;

class HomogeneousMedium
{
public:
	Colour sigmaA;
	Colour sigmaS;
	float g;

	HomogeneousMedium()
		: sigmaA(0.0f, 0.0f, 0.0f), sigmaS(0.0f, 0.0f, 0.0f), g(0.0f)
	{
	}

	HomogeneousMedium(Colour _sigmaA, Colour _sigmaS, float _g)
		: sigmaA(nonNegative(_sigmaA)), sigmaS(nonNegative(_sigmaS)),
		  g(std::max(-0.999f, std::min(0.999f, _g)))
	{
	}

	Colour sigmaT() const
	{
		return sigmaA + sigmaS;
	}

	Colour transmittance(float distance) const
	{
		Colour st = sigmaT();
		return Colour(
			st.r > 0.0f ? expf(-st.r * distance) : 1.0f,
			st.g > 0.0f ? expf(-st.g * distance) : 1.0f,
			st.b > 0.0f ? expf(-st.b * distance) : 1.0f);
	}

	// Returns true for a real scattering event and false when the ray reaches the surface.
	// The returned RGB weight already includes transmittance and the sampling PDF.
	bool sampleDistance(float distanceToSurface, Sampler* sampler,
		float& distance, Colour& weight) const
	{
		Colour st = sigmaT();
		int channel = std::min(2, static_cast<int>(sampler->next() * 3.0f));
		float channelExtinction = component(st, channel);

		if (channelExtinction > 0.0f)
		{
			float u = std::min(sampler->next(), std::nextafter(1.0f, 0.0f));
			distance = -logf(1.0f - u) / channelExtinction;
		} else
		{
			distance = FLT_MAX;
		}

		if (distance < distanceToSurface)
		{
			Colour tr = transmittance(distance);
			float pdf = (st.r * tr.r + st.g * tr.g + st.b * tr.b) / 3.0f;
			if (pdf <= 0.0f)
			{
				weight = Colour(0.0f, 0.0f, 0.0f);
				return true;
			}
			weight = tr * sigmaS / pdf;
			return true;
		}

		distance = distanceToSurface;
		Colour tr = transmittance(distanceToSurface);
		float pdf = (tr.r + tr.g + tr.b) / 3.0f;
		weight = pdf > 0.0f ? tr / pdf : Colour(0.0f, 0.0f, 0.0f);
		return false;
	}

	float phase(const Vec3& forward, const Vec3& wi) const
	{
		float cosTheta = std::max(-1.0f, std::min(1.0f, Dot(forward, wi)));
		float denominator = 1.0f + g * g - 2.0f * g * cosTheta;
		return (1.0f - g * g) /
			(4.0f * M_PI * denominator * sqrtf(denominator));
	}

	Vec3 samplePhase(const Vec3& forward, Sampler* sampler, float& pdf) const
	{
		float u1 = sampler->next();
		float u2 = sampler->next();
		float cosTheta;
		if (fabsf(g) < 1e-3f)
		{
			cosTheta = 1.0f - 2.0f * u1;
		} else
		{
			float ratio = (1.0f - g * g) / (1.0f - g + 2.0f * g * u1);
			cosTheta = (1.0f + g * g - ratio * ratio) / (2.0f * g);
			cosTheta = std::max(-1.0f, std::min(1.0f, cosTheta));
		}

		float sinTheta = sqrtf(std::max(0.0f, 1.0f - cosTheta * cosTheta));
		float phi = 2.0f * M_PI * u2;
		Vec3 local(sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta);
		Frame frame;
		frame.fromVector(forward);
		Vec3 wi = frame.toWorld(local).normalize();
		pdf = phase(forward, wi);
		return wi;
	}

private:
	static Colour nonNegative(const Colour& c)
	{
		return Colour(
			std::max(0.0f, c.r),
			std::max(0.0f, c.g),
			std::max(0.0f, c.b));
	}

	static float component(const Colour& c, int channel)
	{
		if (channel == 0) return c.r;
		if (channel == 1) return c.g;
		return c.b;
	}
};

class ShadingData
{
public:
	Vec3 x;
	Vec3 wo;
	Vec3 sNormal;
	Vec3 gNormal;
	float tu;
	float tv;
	Frame frame;
	BSDF* bsdf;
	float t;
	ShadingData() {}
	ShadingData(Vec3 _x, Vec3 n)
	{
		x = _x;
		gNormal = n;
		sNormal = n;
		bsdf = NULL;
	}
};

class ShadingHelper
{
public:
	static float fresnelDielectric(float cosTheta_i, float iorInt, float iorExt)
	{
		float eta = iorExt / iorInt;
		if (cosTheta_i < 0.0f) {
			//exiting
			eta = iorInt / iorExt;
		}
		cosTheta_i = std::abs(cosTheta_i);
		float sinTheta_i = sqrtf(1.0f - cosTheta_i * cosTheta_i);
		float sinTheta_t = eta * sinTheta_i;
		if (sinTheta_t >= 1.0f) return 1.0f; // TIR
		float cosTheta_t = sqrtf(1.0f - sinTheta_t * sinTheta_t);
		float fresnelParl = (cosTheta_i - eta * cosTheta_t) / ( cosTheta_i + (eta * cosTheta_t));
		float fresnelPerp = (eta * cosTheta_i - cosTheta_t) / (eta * cosTheta_i + cosTheta_t);
		return (fresnelParl * fresnelParl + fresnelPerp * fresnelPerp) * 0.5f;
	}
	static Colour fresnelConductor(float cosTheta, Colour ior, Colour k)
	{
		Colour cos_c(cosTheta, cosTheta, cosTheta);
		Colour sin2_c(1 - cosTheta * cosTheta, 1 - cosTheta * cosTheta, 1 - cosTheta * cosTheta);

		Colour fresnelParl2 = ((ior * ior + k * k) * cos_c * cos_c - ior * (2.0f * cosTheta) + sin2_c) / ((ior * ior + k * k) * cos_c * cos_c + ior * (2.0f * cosTheta) + sin2_c);
		Colour fresnelPerp2 = ((ior * ior + k * k) - ior * (2.0f * cosTheta) + cos_c * cos_c) / ((ior * ior + k * k) + ior * (2.0f * cosTheta) + cos_c * cos_c);
		return (fresnelParl2 + fresnelPerp2) * 0.5f;
	}
	static float lambdaGGX(Vec3 w, float alpha)
	{
		//tan^2=sin^2/cos^2
		float tan2Theta = (1.0f - w.z * w.z) / (w.z * w.z);
		float a2Tan2Theta = alpha * alpha * tan2Theta;
		return (std::sqrt(1.0f + a2Tan2Theta) - 1.0f) / 2.0f;
	}
	static float Gggx(Vec3 wi, Vec3 wo, float alpha)
	{
		return 1.0f / (1.0f + lambdaGGX(wi, alpha)) / (1.0f + lambdaGGX(wo, alpha));
	}
	static float Dggx(Vec3 h, float alpha)
	{
		return alpha * alpha / (M_PI * (h.z * h.z * (alpha * alpha - 1.0f) + 1) * (h.z * h.z * (alpha * alpha - 1.0f) + 1));
	}
};

class BSDF
{
public:
	Colour emission;
	virtual Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf) = 0;
	virtual Colour evaluate(const ShadingData& shadingData, const Vec3& wi) = 0;
	virtual float PDF(const ShadingData& shadingData, const Vec3& wi) = 0;
	virtual bool isPureSpecular() = 0;
	virtual bool isTwoSided() = 0;
	virtual const HomogeneousMedium* interiorMedium() const
	{
		return nullptr;
	}
	virtual bool supportsStraightTransmission() const
	{
		return false;
	}
	virtual Colour straightTransmission(const ShadingData& shadingData) const
	{
		return Colour(0.0f, 0.0f, 0.0f);
	}
	bool isLight()
	{
		return emission.Lum() > 0 ? true : false;
	}
	void addLight(Colour _emission)
	{
		emission = _emission;
	}
	Colour emit(const ShadingData& shadingData, const Vec3& wi)
	{
		return emission;
	}
	virtual float mask(const ShadingData& shadingData) = 0;
};
class DiffuseBSDF : public BSDF
{
public:
	Texture* albedo;
	DiffuseBSDF() = default;
	DiffuseBSDF(Texture* _albedo)
	{
		albedo = _albedo;
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		Vec3 wiLocal = SamplingDistributions::cosineSampleHemisphere(sampler->next(), sampler->next());
		pdf = SamplingDistributions::cosineHemispherePDF(wiLocal);
		reflectedColour = albedo->sample(shadingData.tu, shadingData.tv) * M_1_PI;
		Vec3 wi = shadingData.frame.toWorld(wiLocal);
		return wi;
	}
	Colour evaluate(const ShadingData& shadingData, const Vec3& wi)
	{
		return albedo->sample(shadingData.tu, shadingData.tv) * M_1_PI;
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		Vec3 wiLocal = shadingData.frame.toLocal(wi);
		return SamplingDistributions::cosineHemispherePDF(wiLocal);
	}
	bool isPureSpecular()
	{
		return false;
	}
	bool isTwoSided()
	{
		return true;
	}
	float mask(const ShadingData& shadingData)
	{
		return albedo->sampleAlpha(shadingData.tu, shadingData.tv);
	}
};

class MirrorBSDF : public BSDF
{
public:
	Texture* albedo;
	MirrorBSDF() = default;
	MirrorBSDF(Texture* _albedo)
	{
		albedo = _albedo;
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		Vec3 wiLocal = shadingData.frame.toLocal(shadingData.wo);
		wiLocal.x = -wiLocal.x;
		wiLocal.y = -wiLocal.y;
		pdf = 1.0f;
		reflectedColour = albedo->sample(shadingData.tu, shadingData.tv)/ std::max(wiLocal.z, 1e-6f);
		Vec3 wi = shadingData.frame.toWorld(wiLocal);
		return wi;
	}
	Colour evaluate(const ShadingData& shadingData, const Vec3& wi)
	{
		return Colour(0.0f, 0.0f, 0.0f);
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		return 0.0f;
	}
	bool isPureSpecular()
	{
		return true;
	}
	bool isTwoSided()
	{
		return true;
	}
	float mask(const ShadingData& shadingData)
	{
		return albedo->sampleAlpha(shadingData.tu, shadingData.tv);
	}
};

class ConductorBSDF : public BSDF
{
public:
	Texture* albedo;
	Colour eta;
	Colour k;
	float alpha;
	ConductorBSDF() = default;
	ConductorBSDF(Texture* _albedo, Colour _eta, Colour _k, float roughness)
	{
		albedo = _albedo;
		eta = _eta;
		k = _k;
		alpha = 1.62142f * sqrtf(roughness);
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		Vec3 wo = shadingData.frame.toLocal(shadingData.wo);
		float r1 = sampler->next();
		float r2 = sampler->next();
		float cosTheta = std::sqrt((1.0f - r1) / (r1 * (alpha * alpha - 1.0f) + 1));
		float phi = 2.0f * M_PI * r2;
		float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
		Vec3 wm(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
		Vec3 wi = -wo + wm * 2.0f * Dot(wo, wm);
		if (wi.z <= 0.0f) { pdf = 0.0f; return Vec3(0,0,0); }
		float D = ShadingHelper::Dggx(wm, alpha);
		float G = ShadingHelper::Gggx(wi, wo, alpha);
		Colour F = ShadingHelper::fresnelConductor(Dot(wi, wm), eta, k);
		pdf = D * wm.z / (4.0f * Dot(wo, wm));
		reflectedColour = albedo->sample(shadingData.tu, shadingData.tv) * F * D * G / (4.0f * wo.z * wi.z);
		return shadingData.frame.toWorld(wi);
	}
	Colour evaluate(const ShadingData& shadingData, const Vec3& wiWorld)
	{
		Vec3 wo = shadingData.frame.toLocal(shadingData.wo);
		Vec3 wi = shadingData.frame.toLocal(wiWorld);
		Vec3 wm = (wo + wi).normalize();
		float D = ShadingHelper::Dggx(wm, alpha);
		float G = ShadingHelper::Gggx(wi, wo, alpha);
		Colour F = ShadingHelper::fresnelConductor(Dot(wi, wm), eta, k);
		return albedo->sample(shadingData.tu, shadingData.tv) * F * D * G / (4.0f * wo.z * wi.z);
	}
	float PDF(const ShadingData& shadingData, const Vec3& wiWorld)
	{
		Vec3 wo = shadingData.frame.toLocal(shadingData.wo);
		Vec3 wi = shadingData.frame.toLocal(wiWorld);
		Vec3 wm = (wo + wi).normalize();
		float D = ShadingHelper::Dggx(wm, alpha);
		return D * wm.z / (4.0f * Dot(wo, wm));
	}
	bool isPureSpecular()
	{
		return false;
	}
	bool isTwoSided()
	{
		return true;
	}
	float mask(const ShadingData& shadingData)
	{
		return albedo->sampleAlpha(shadingData.tu, shadingData.tv);
	}
};

class GlassBSDF : public BSDF
{
public:
	Texture* albedo;
	float intIOR;
	float extIOR;
	GlassBSDF() = default;
	GlassBSDF(Texture* _albedo, float _intIOR, float _extIOR)
	{
		albedo = _albedo;
		intIOR = _intIOR;
		extIOR = _extIOR;
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		Vec3 woLocal = shadingData.frame.toLocal(shadingData.wo);
		float etaI = extIOR;
		float etaT = intIOR;
		if (woLocal.z < 0.0f) {
			etaI = intIOR;
			etaT = extIOR;
		}
		float F = ShadingHelper::fresnelDielectric(woLocal.z, intIOR, extIOR);

		if (sampler->next() < F) { //reflection
			Vec3 wiLocal(-woLocal.x, -woLocal.y, woLocal.z);
			pdf = F;
			reflectedColour = albedo->sample(shadingData.tu, shadingData.tv) * F / std::max(1e-6f, std::abs(wiLocal.z));
			return shadingData.frame.toWorld(wiLocal);
		} else {//refraction
			float eta = etaI / etaT;
			float sin2Theta_i = 1.0f - woLocal.z * woLocal.z;
			float sin2Theta_t = eta * eta * sin2Theta_i;
			float cosTheta_t = std::sqrt(1.0f - sin2Theta_t);
			if (woLocal.z < 0) cosTheta_t = -cosTheta_t;
			Vec3 wiLocal(-woLocal.x * eta, -woLocal.y * eta, -cosTheta_t);
			pdf = 1.0f - F;
			reflectedColour = albedo->sample(shadingData.tu, shadingData.tv) * (etaT * etaT / (etaI * etaI)) * (1.0f - F) / std::max(1e-6f, std::abs(wiLocal.z));
			return shadingData.frame.toWorld(wiLocal);
		}
	}
	Colour evaluate(const ShadingData& shadingData, const Vec3& wi)
	{
		return Colour(0.0f, 0.0f, 0.0f);
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		return 0.0f;
	}
	bool isPureSpecular()
	{
		return true;
	}
	bool isTwoSided()
	{
		return false;
	}
	float mask(const ShadingData& shadingData)
	{
		return albedo->sampleAlpha(shadingData.tu, shadingData.tv);
	}
};

class HomogeneousMediumBSDF : public GlassBSDF
{
public:
	HomogeneousMedium medium;

	HomogeneousMediumBSDF(Texture* _albedo, float _intIOR, float _extIOR,
		Colour _sigmaA, Colour _sigmaS, float _g)
		: GlassBSDF(_albedo, _intIOR, _extIOR), medium(_sigmaA, _sigmaS, _g)
	{
	}

	// Preserve the outward-facing mesh normal so entering and leaving can be distinguished.
	bool isTwoSided()
	{
		return false;
	}

	const HomogeneousMedium* interiorMedium() const
	{
		return &medium;
	}

	bool supportsStraightTransmission() const
	{
		return intIOR == extIOR;
	}

	Colour straightTransmission(const ShadingData& shadingData) const
	{
		return albedo->sample(shadingData.tu, shadingData.tv);
	}
};

class DielectricBSDF : public BSDF
{
public:
	Texture* albedo;
	float intIOR;
	float extIOR;
	float alpha;
	DielectricBSDF() = default;
	DielectricBSDF(Texture* _albedo, float _intIOR, float _extIOR, float roughness)
	{
		albedo = _albedo;
		intIOR = _intIOR;
		extIOR = _extIOR;
		alpha = 1.62142f * sqrtf(roughness);
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		// Replace this with Dielectric sampling code
		Vec3 wi = SamplingDistributions::cosineSampleHemisphere(sampler->next(), sampler->next());
		pdf = wi.z / M_PI;
		reflectedColour = albedo->sample(shadingData.tu, shadingData.tv) / M_PI;
		wi = shadingData.frame.toWorld(wi);
		return wi;
	}
	Colour evaluate(const ShadingData& shadingData, const Vec3& wi)
	{
		// Replace this with Dielectric evaluation code
		return albedo->sample(shadingData.tu, shadingData.tv) / M_PI;
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		// Replace this with Dielectric PDF
		Vec3 wiLocal = shadingData.frame.toLocal(wi);
		return SamplingDistributions::cosineHemispherePDF(wiLocal);
	}
	bool isPureSpecular()
	{
		return false;
	}
	bool isTwoSided()
	{
		return false;
	}
	float mask(const ShadingData& shadingData)
	{
		return albedo->sampleAlpha(shadingData.tu, shadingData.tv);
	}
};

class OrenNayarBSDF : public BSDF
{
public:
	Texture* albedo;
	float sigma;
	OrenNayarBSDF() = default;
	OrenNayarBSDF(Texture* _albedo, float _sigma)
	{
		albedo = _albedo;
		sigma = _sigma;
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		// Replace this with OrenNayar sampling code
		Vec3 wi = SamplingDistributions::cosineSampleHemisphere(sampler->next(), sampler->next());
		pdf = wi.z / M_PI;
		reflectedColour = albedo->sample(shadingData.tu, shadingData.tv) / M_PI;
		wi = shadingData.frame.toWorld(wi);
		return wi;
	}
	Colour evaluate(const ShadingData& shadingData, const Vec3& wi)
	{
		// Replace this with OrenNayar evaluation code
		return albedo->sample(shadingData.tu, shadingData.tv) / M_PI;
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		// Replace this with OrenNayar PDF
		Vec3 wiLocal = shadingData.frame.toLocal(wi);
		return SamplingDistributions::cosineHemispherePDF(wiLocal);
	}
	bool isPureSpecular()
	{
		return false;
	}
	bool isTwoSided()
	{
		return true;
	}
	float mask(const ShadingData& shadingData)
	{
		return albedo->sampleAlpha(shadingData.tu, shadingData.tv);
	}
};

class PlasticBSDF : public BSDF
{
	//Using Phong model
public:
	Texture* albedo;
	float intIOR;
	float extIOR;
	float alpha;
	PlasticBSDF() = default;
	PlasticBSDF(Texture* _albedo, float _intIOR, float _extIOR, float roughness)
	{
		albedo = _albedo;
		intIOR = _intIOR;
		extIOR = _extIOR;
		alpha = 1.62142f * sqrtf(roughness);
	}
	float alphaToPhongExponent()
	{
		return (2.0f / SQ(std::max(alpha, 0.001f))) - 2.0f;
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		Vec3 woLocal = shadingData.frame.toLocal(shadingData.wo);
		float e = alphaToPhongExponent();
		float ks = 0.3f, kd = 0.7f; //assume ks=0.3
		//sample a lobe around the reflection direction
		float r1 = sampler->next();
		float r2 = sampler->next();
		float cosTheta = std::pow(r1, 1.0f / (e + 1.0f));
		float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
		float phi = 2.0f * M_PI * r2;
		Vec3 lobeLocal(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
		Vec3 wr(-woLocal.x, -woLocal.y, woLocal.z);
		// Build frame around wr and transform lobeLocal
		Frame wrFrame;
		wrFrame.fromVector(wr);
		Vec3 wi = wrFrame.toWorld(lobeLocal);
		float cosAlpha = std::max(0.0f, Dot(wr, wi));
		pdf = ks * ((e + 1.0f) / (2.0f * M_PI) * std::pow(cosAlpha, e)) + kd * M_1_PI * 0.5f;
		reflectedColour = Colour(1.f, 1.f, 1.f) * ks * (e + 2.0f) / (2.0f * M_PI) * std::pow(cosAlpha, e) + albedo->sample(shadingData.tu, shadingData.tv) * kd * M_1_PI;
		Vec3 wiWorld = shadingData.frame.toWorld(wi);
		return wiWorld;
	}
	Colour evaluate(const ShadingData& shadingData, const Vec3& wiWorld)
	{
		Vec3 wo = shadingData.frame.toLocal(shadingData.wo);
		Vec3 wi = shadingData.frame.toLocal(wiWorld);
		float e = alphaToPhongExponent();
		Vec3 wr(-wo.x, -wo.y, wo.z);
		float cosAlpha = std::max(0.0f, Dot(wr, wi));
		float ks = 0.3f, kd = 0.7f;
		Colour spec = Colour(1.f,1.f,1.f) * ks * (e + 2.0f) / (2.0f * M_PI) * std::pow(cosAlpha, e);
		Colour diff = albedo->sample(shadingData.tu, shadingData.tv) * kd * M_1_PI;
		return spec + diff;
	}
	float PDF(const ShadingData& shadingData, const Vec3& wiWorld)
	{
		Vec3 wo = shadingData.frame.toLocal(shadingData.wo);
		Vec3 wi = shadingData.frame.toLocal(wiWorld);
		float e = alphaToPhongExponent();
		Vec3 wr(-wo.x, -wo.y, wo.z);
		float cosAlpha = std::max(0.0f, Dot(wr, wi));
		float pdfSpec = (e + 1.0f) / (2.0f * M_PI) * std::pow(cosAlpha, e);
		float pdfDiff = 0.5 * M_1_PI;
		return 0.3f * pdfSpec + 0.7f * pdfDiff;
	}
	bool isPureSpecular()
	{
		return false;
	}
	bool isTwoSided()
	{
		return true;
	}
	float mask(const ShadingData& shadingData)
	{
		return albedo->sampleAlpha(shadingData.tu, shadingData.tv);
	}
};

class LayeredBSDF : public BSDF
{
public:
	BSDF* base;
	Colour sigmaa;
	float thickness;
	float intIOR;
	float extIOR;
	LayeredBSDF() = default;
	LayeredBSDF(BSDF* _base, Colour _sigmaa, float _thickness, float _intIOR, float _extIOR)
	{
		base = _base;
		sigmaa = _sigmaa;
		thickness = _thickness;
		intIOR = _intIOR;
		extIOR = _extIOR;
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		// Add code to include layered sampling
		return base->sample(shadingData, sampler, reflectedColour, pdf);
	}
	Colour evaluate(const ShadingData& shadingData, const Vec3& wi)
	{
		// Add code for evaluation of layer
		return base->evaluate(shadingData, wi);
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		// Add code to include PDF for sampling layered BSDF
		return base->PDF(shadingData, wi);
	}
	bool isPureSpecular()
	{
		return base->isPureSpecular();
	}
	bool isTwoSided()
	{
		return true;
	}
	float mask(const ShadingData& shadingData)
	{
		return base->mask(shadingData);
	}
};

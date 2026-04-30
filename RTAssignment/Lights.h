#pragma once

#include "Core.h"
#include "Geometry.h"
#include "Materials.h"
#include "Sampling.h"

#pragma warning( disable : 4244)

class SceneBounds
{
public:
	Vec3 sceneCentre;
	float sceneRadius;
};

class Light
{
public:
	virtual Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& emittedColour, float& pdf) = 0;
	virtual Colour evaluate(const Vec3& wi) = 0;
	virtual float PDF(const ShadingData& shadingData, const Vec3& wi) = 0;
	virtual bool isArea() = 0;
	virtual Vec3 normal(const ShadingData& shadingData, const Vec3& wi) = 0;
	virtual float totalIntegratedPower() = 0;
	virtual Vec3 samplePositionFromLight(Sampler* sampler, float& pdf) = 0;
	virtual Vec3 sampleDirectionFromLight(Sampler* sampler, float& pdf) = 0;
};

class AreaLight : public Light
{
public:
	Triangle* triangle = NULL;
	Colour emission;
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& emittedColour, float& pdf)
	{
		emittedColour = emission;
		return triangle->sample(sampler, pdf);
	}
	Colour evaluate(const Vec3& wi)
	{
		if (Dot(wi, triangle->gNormal()) < 0)
		{
			return emission;
		}
		return Colour(0.0f, 0.0f, 0.0f);
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		return 1.0f / triangle->area;
	}
	bool isArea()
	{
		return true;
	}
	Vec3 normal(const ShadingData& shadingData, const Vec3& wi)
	{
		return triangle->gNormal();
	}
	float totalIntegratedPower()
	{
		return (triangle->area * emission.Lum());
	}
	Vec3 samplePositionFromLight(Sampler* sampler, float& pdf)
	{
		return triangle->sample(sampler, pdf);
	}
	Vec3 sampleDirectionFromLight(Sampler* sampler, float& pdf)
	{
		// Cosine sampling
		Vec3 wi = SamplingDistributions::cosineSampleHemisphere(sampler->next(), sampler->next());
		pdf = SamplingDistributions::cosineHemispherePDF(wi);
		Frame frame;
		frame.fromVector(triangle->gNormal());
		return frame.toWorld(wi);
	}
};

class BackgroundColour : public Light
{
public:
	Colour emission;
	BackgroundColour(Colour _emission)
	{
		emission = _emission;
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		Vec3 wi = SamplingDistributions::uniformSampleSphere(sampler->next(), sampler->next());
		pdf = SamplingDistributions::uniformSpherePDF(wi);
		reflectedColour = emission;
		return wi;
	}
	Colour evaluate(const Vec3& wi)
	{
		return emission;
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		return SamplingDistributions::uniformSpherePDF(wi);
	}
	bool isArea()
	{
		return false;
	}
	Vec3 normal(const ShadingData& shadingData, const Vec3& wi)
	{
		return -wi;
	}
	float totalIntegratedPower()
	{
		return emission.Lum() * 4.0f * M_PI;
	}
	Vec3 samplePositionFromLight(Sampler* sampler, float& pdf)
	{
		Vec3 p = SamplingDistributions::uniformSampleSphere(sampler->next(), sampler->next());
		p = p * use<SceneBounds>().sceneRadius;
		p = p + use<SceneBounds>().sceneCentre;
		pdf = 1.0f / (4 * M_PI * SQ(use<SceneBounds>().sceneRadius));
		return p;
	}
	Vec3 sampleDirectionFromLight(Sampler* sampler, float& pdf)
	{
		Vec3 wi = SamplingDistributions::uniformSampleSphere(sampler->next(), sampler->next());
		pdf = SamplingDistributions::uniformSpherePDF(wi);
		return wi;
	}
};

class EnvironmentMap : public Light
{
public:
	Texture* env;
	std::vector<float> marginalPDF;// P(vIndex)
	std::vector<float> marginalCDF;
	std::vector<std::vector<float>> conditionalPDF;//P(uIndex|vIndex)
	std::vector<std::vector<float>> conditionalCDF;
	EnvironmentMap(Texture* _env)
	{
		env = _env;
		int w = env->width;
		int h = env->height;
		marginalPDF.resize(h, 0.0f);
		marginalCDF.resize(h + 1, 0.0f);
		conditionalPDF.resize(h, std::vector<float>(w, 0.0f));
		conditionalCDF.resize(h, std::vector<float>(w + 1, 0.0f));
		std::vector<float> rowWeights(h, 0.0f);
		float totalWeight = 0.0f;

		// build row weights and conditional pdf each row
		for (int i = 0; i < h; ++i)
		{
			float sinTheta = sinf((i + 0.5f) / (float)h * M_PI);
			float rowWeight = 0.0f;
			for (int j = 0; j < w; ++j)
			{
				// weight = Lum * sin(theta)
				float weight = env->texels[(i * w) + j].Lum() * sinTheta;
				conditionalPDF[i][j] = weight;//will be normalized
				rowWeight += weight;
			}
			rowWeights[i] = rowWeight;
			totalWeight += rowWeight;

			// build CDF(u|v)
			if (rowWeight > 1e-7)
			{
				for (int j = 0; j < w; j++)
				{
					conditionalPDF[i][j] /= rowWeight;
					conditionalCDF[i][j + 1] = conditionalCDF[i][j] + conditionalPDF[i][j];
				}
			}
			else
			{
				// total black
				for (int j = 0; j < w; j++)
				{
					conditionalPDF[i][j] = 1.0f / w;
					conditionalCDF[i][j + 1] = (float)(j + 1) / w;
				}
			}

		}
		//build marginal pdf and cdf
		if (totalWeight > 0.0f)
		{
			for (int j = 0; j < h; ++j)
			{
				marginalPDF[j] = rowWeights[j] / totalWeight;
				marginalCDF[j + 1] = marginalCDF[j] + marginalPDF[j];;
			}
		}
		else
		{
			for (int j = 0; j < h; ++j)
			{
				marginalPDF[j] = 1.0f / h;
				marginalCDF[j + 1] = (float)(j + 1) / h;
			}
		}
	}
	Vec3 sample(const ShadingData& shadingData, Sampler* sampler, Colour& reflectedColour, float& pdf)
	{
		float vSample = sampler->next();
		float uSample = sampler->next();
		int vIndex = std::upper_bound(marginalCDF.begin(), marginalCDF.end(), vSample) - marginalCDF.begin() - 1;//b-search vSample
		vIndex = std::min(std::max(vIndex, 0), env->height - 1);//clamp
		int uIndex = std::upper_bound(conditionalCDF[vIndex].begin(), conditionalCDF[vIndex].end(), uSample) - conditionalCDF[vIndex].begin() - 1;
		uIndex = std::min(std::max(uIndex, 0), env->width - 1);
		float u = ((float)uIndex) / (float)env->width;
		float v = ((float)vIndex) / (float)env->height;
		float phi = u * 2.0f * M_PI;
		float theta = v * M_PI;
		float sinTheta = sinf(theta);
		// Use Y-up coordinate
		Vec3 wi = Vec3(cosf(phi) * sinTheta, cosf(theta), sinf(phi) * sinTheta);
		pdf = marginalPDF[vIndex] * conditionalPDF[vIndex][uIndex] * env->width * env->height / (2.0f * M_PI * M_PI * sinTheta);
		reflectedColour = env->texels[vIndex * env->width + uIndex];
		return wi;

	}
	Colour evaluate(const Vec3& wi)
	{
		float u = atan2f(wi.z, wi.x);
		u = (u < 0.0f) ? u + (2.0f * M_PI) : u;
		u = u / (2.0f * M_PI);
		float v = acosf(wi.y) / M_PI;
		return env->sample(u, v);
	}
	float PDF(const ShadingData& shadingData, const Vec3& wi)
	{
		float u = atan2f(wi.z, wi.x);
		u = (u < 0.0f) ? u + (2.0f * M_PI) : u;
		u = u / (2.0f * M_PI);
		float v = acosf(wi.y) / M_PI;
		int uIndex = std::max(0, std::min((int)(u * env->width), env->width - 1));
		int vIndex = std::max(0, std::min((int)(v * env->height), env->height - 1));
		// P(u, v) = P(vIndex) * P(uIndex|vIndex) * w * h;
		float probUV = marginalPDF[vIndex] * conditionalPDF[vIndex][uIndex] * env->width * env->height;
		float sinTheta = sinf(v * M_PI);
		if (sinTheta <= 1e-7f) return 0.0f;
		return probUV / (2.0f * M_PI * M_PI * sinTheta);
	}
	bool isArea()
	{
		return false;
	}
	Vec3 normal(const ShadingData& shadingData, const Vec3& wi)
	{
		return -wi;
	}
	float totalIntegratedPower()
	{
		float total = 0;
		for (int i = 0; i < env->height; i++)
		{
			float st = sinf(((float)i / (float)env->height) * M_PI);
			for (int n = 0; n < env->width; n++)
			{
				total += (env->texels[(i * env->width) + n].Lum() * st);
			}
		}
		total = total / (float)(env->width * env->height);
		return total * 4.0f * M_PI;
	}
	Vec3 samplePositionFromLight(Sampler* sampler, float& pdf)
	{
		// Samples a point on the bounding sphere of the scene. Feel free to improve this.
		Vec3 p = SamplingDistributions::uniformSampleSphere(sampler->next(), sampler->next());
		p = p * use<SceneBounds>().sceneRadius;
		p = p + use<SceneBounds>().sceneCentre;
		pdf = 1.0f / (4 * M_PI * SQ(use<SceneBounds>().sceneRadius));
		return p;
	}
	Vec3 sampleDirectionFromLight(Sampler* sampler, float& pdf)
	{
		float vSample = sampler->next();
		float uSample = sampler->next();
		int vIndex = std::upper_bound(marginalCDF.begin(), marginalCDF.end(), vSample) - marginalCDF.begin() - 1;
		vIndex = std::min(std::max(vIndex, 0), env->height - 1);
		int uIndex = std::upper_bound(conditionalCDF[vIndex].begin(), conditionalCDF[vIndex].end(), uSample) - conditionalCDF[vIndex].begin() - 1;
		uIndex = std::min(std::max(uIndex, 0), env->width - 1);
		float u = ((float)uIndex) / (float)env->width;
		float v = ((float)vIndex) / (float)env->height;
		float phi = u * 2.0f * M_PI;
		float theta = v * M_PI;
		float sinTheta = sinf(theta);
		// direction into the scene
		Vec3 wi = -Vec3(cosf(phi) * sinTheta, cosf(theta), sinf(phi) * sinTheta);
		pdf = marginalPDF[vIndex] * conditionalPDF[vIndex][uIndex] * env->width * env->height / (2.0f * M_PI * M_PI * sinTheta);
		return wi;
	}
};
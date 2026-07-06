#include "Materials.h"

#include <cmath>
#include <iostream>

namespace
{
bool closeEnough(float actual, float expected, float tolerance)
{
	return std::fabs(actual - expected) <= tolerance;
}

bool testDistanceEstimator()
{
	HomogeneousMedium medium(
		Colour(0.2f, 0.5f, 0.9f),
		Colour(1.0f, 2.0f, 3.0f),
		0.0f);
	MTRandom sampler(17);
	const float distanceToSurface = 0.7f;
	const int sampleCount = 200000;
	Colour surfaceEstimate(0.0f, 0.0f, 0.0f);
	Colour scatteringEstimate(0.0f, 0.0f, 0.0f);

	for (int i = 0; i < sampleCount; ++i)
	{
		float distance;
		Colour weight;
		bool scattered = medium.sampleDistance(
			distanceToSurface, &sampler, distance, weight);
		if (!scattered)
		{
			surfaceEstimate = surfaceEstimate + weight;
		} else
		{
			scatteringEstimate = scatteringEstimate + weight;
		}
	}

	surfaceEstimate = surfaceEstimate / static_cast<float>(sampleCount);
	scatteringEstimate = scatteringEstimate / static_cast<float>(sampleCount);
	Colour expectedSurface = medium.transmittance(distanceToSurface);
	Colour st = medium.sigmaT();
	Colour expectedScattering(
		medium.sigmaS.r / st.r * (1.0f - expectedSurface.r),
		medium.sigmaS.g / st.g * (1.0f - expectedSurface.g),
		medium.sigmaS.b / st.b * (1.0f - expectedSurface.b));
	const float tolerance = 0.008f;
	return closeEnough(surfaceEstimate.r, expectedSurface.r, tolerance)
		&& closeEnough(surfaceEstimate.g, expectedSurface.g, tolerance)
		&& closeEnough(surfaceEstimate.b, expectedSurface.b, tolerance)
		&& closeEnough(scatteringEstimate.r, expectedScattering.r, tolerance)
		&& closeEnough(scatteringEstimate.g, expectedScattering.g, tolerance)
		&& closeEnough(scatteringEstimate.b, expectedScattering.b, tolerance);
}

bool testHenyeyGreensteinMeanCosine()
{
	const float expectedMean = 0.65f;
	HomogeneousMedium medium(
		Colour(0.0f, 0.0f, 0.0f),
		Colour(1.0f, 1.0f, 1.0f),
		expectedMean);
	MTRandom sampler(29);
	Vec3 forward(0.0f, 0.0f, 1.0f);
	const int sampleCount = 200000;
	float meanCosine = 0.0f;

	for (int i = 0; i < sampleCount; ++i)
	{
		float pdf;
		Vec3 wi = medium.samplePhase(forward, &sampler, pdf);
		if (!(pdf > 0.0f) || !std::isfinite(pdf))
		{
			return false;
		}
		meanCosine += Dot(forward, wi);
	}

	meanCosine /= static_cast<float>(sampleCount);
	return closeEnough(meanCosine, expectedMean, 0.006f);
}

bool testVacuum()
{
	HomogeneousMedium vacuum;
	MTRandom sampler(41);
	float distance;
	Colour weight;
	bool scattered = vacuum.sampleDistance(2.0f, &sampler, distance, weight);
	return !scattered
		&& closeEnough(distance, 2.0f, 1e-6f)
		&& closeEnough(weight.r, 1.0f, 1e-6f)
		&& closeEnough(weight.g, 1.0f, 1e-6f)
		&& closeEnough(weight.b, 1.0f, 1e-6f);
}

bool testStraightBoundaryRequirement()
{
	Colour sigmaA(0.1f, 0.1f, 0.1f);
	Colour sigmaS(1.0f, 1.0f, 1.0f);
	HomogeneousMediumBSDF matched(
		nullptr, 1.0f, 1.0f, sigmaA, sigmaS, 0.0f);
	HomogeneousMediumBSDF refractive(
		nullptr, 1.3f, 1.0f, sigmaA, sigmaS, 0.0f);
	return matched.supportsStraightTransmission()
		&& !refractive.supportsStraightTransmission();
}

bool testRefractiveMediumDirectTransmission()
{
	Texture white;
	white.alpha = nullptr;
	white.loadDefault();
	HomogeneousMediumBSDF refractive(
		&white, 1.5f, 1.0f,
		Colour(0.1f, 0.1f, 0.1f),
		Colour(1.0f, 1.0f, 1.0f), 0.0f);
	ShadingData boundary = {};
	boundary.wo = Vec3(0.0f, 0.0f, -1.0f);
	boundary.sNormal = Vec3(0.0f, 0.0f, 1.0f);
	Colour transmission = refractive.mediumDirectTransmission(boundary);
	return refractive.supportsMediumDirectTransmission()
		&& closeEnough(transmission.r, 0.96f, 1e-5f)
		&& closeEnough(transmission.g, 0.96f, 1e-5f)
		&& closeEnough(transmission.b, 0.96f, 1e-5f);
}
}

int main()
{
	bool passed = true;
	passed = testVacuum() && passed;
	passed = testDistanceEstimator() && passed;
	passed = testHenyeyGreensteinMeanCosine() && passed;
	passed = testStraightBoundaryRequirement() && passed;
	passed = testRefractiveMediumDirectTransmission() && passed;

	if (!passed)
	{
		std::cerr << "Volumetric sampling tests failed.\n";
		return 1;
	}

	std::cout << "Volumetric sampling tests passed.\n";
	return 0;
}

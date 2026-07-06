#pragma once

#include "Core.h"
#include "Sampling.h"
#include "Geometry.h"
#include "Imaging.h"
#include "Materials.h"
#include "Lights.h"
#include "Scene.h"
#include "GamesEngineeringBase.h"
#include <thread>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <condition_variable>

class RayTracer
{
public:
	Scene* scene = nullptr;
	GamesEngineeringBase::Window* canvas = nullptr;
	Film* film = nullptr;
	MTRandom *samplers = nullptr;
	std::thread **threads = nullptr;
	int numProcs = 0;
	
	std::mutex syncMutex;
	std::condition_variable startCV;
	std::condition_variable doneCV;
	bool exitThreads = false;
	int activeWorkers = 0;
	int generation = 0;
	std::atomic<unsigned int> nextTile{ 0 };
	unsigned int totalTiles = 0;
	unsigned int numTilesX = 0;
	const unsigned int tileSize = 32;

	~RayTracer()
	{
		if (threads) {
			{
				std::unique_lock<std::mutex> lock(syncMutex);
				exitThreads = true;
				generation++;
			}
			startCV.notify_all();
			for (int i = 0; i < numProcs; i++) {
				if (threads[i]) {
					threads[i]->join();
					delete threads[i];
				}
			}
			delete[] threads;
		}
		if (samplers) delete[] samplers;
		if (film) delete film;
	}
	void init(Scene* _scene, GamesEngineeringBase::Window* _canvas)
	{
		scene = _scene;
		canvas = _canvas;
		if (film) delete film;
		film = new Film();
		film->init((unsigned int)scene->camera.width, (unsigned int)scene->camera.height, new MitchellNetravaliFilter());
		numProcs = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
		
		if (threads == nullptr) {
			threads = new std::thread*[numProcs];
			samplers = new MTRandom[numProcs];
			exitThreads = false;
			for (int i = 0; i < numProcs; i++) {
				threads[i] = new std::thread(&RayTracer::workerLoop, this, i);
			}
		}
		clear();
	}
	void clear()
	{
		film->clear();
	}
	Colour computeDirect(ShadingData shadingData, Sampler* sampler)
	{
		// If surface is specular we cannot computing direct lighting
		if (shadingData.bsdf->isPureSpecular() == true || scene->lights.empty())
		{
			return Colour(0.0f, 0.0f, 0.0f);
		}
		float pmfLights;
		Light* sampledLight = scene->sampleLight(sampler, pmfLights);
		Colour L_emission;
		float pdfOnLight;
		Vec3 lpos = sampledLight->sample(shadingData, sampler, L_emission, pdfOnLight);
		Vec3 world_wi = (lpos - shadingData.x).normalize();
		if (sampledLight->isArea()) {
			float cosTheta = std::max(0.f, Dot(shadingData.sNormal, world_wi));
			if (cosTheta < 1e-6f) return Colour(0.0f, 0.0f, 0.0f);
			float G_term = std::max(0.f,Dot(-world_wi, sampledLight->normal(shadingData, world_wi))) * cosTheta / Dot(lpos - shadingData.x, lpos - shadingData.x) * scene->visible(lpos, shadingData.x);
			Colour brdf = shadingData.bsdf->evaluate(shadingData, world_wi);
			// MIS computing direct
			float pdfADirect = pdfOnLight * pmfLights;
			float pdfwIndirect = shadingData.bsdf->PDF(shadingData, world_wi);
			float pdfAIndirect = pdfwIndirect / cosTheta * G_term;
			return L_emission * brdf * G_term / (pdfADirect + pdfAIndirect);
		} else {
			// For environment map, sample() returns a direction, not a position.
			Vec3 env_wi = lpos; 
			Ray ray(shadingData.x + env_wi * EPSILON, env_wi);
			if (scene->bvh->traverseVisible(ray, scene->triangles, FLT_MAX)) {
				Colour brdf = shadingData.bsdf->evaluate(shadingData, env_wi);
				float G_term = std::max(0.f, Dot(shadingData.sNormal, env_wi));
				float pdfwDirect = pdfOnLight * pmfLights; 
				float pdfwIndirect = shadingData.bsdf->PDF(shadingData, env_wi);
				return L_emission * brdf * G_term / (pdfwDirect + pdfwIndirect);
			}
		}
		return Colour(0.0f, 0.0f, 0.0f);
	}
	Colour mediumShadowTransmittance(const Vec3& point, const Vec3& wi,
		float distanceToLight, const Vec3& lightPoint, bool finiteLight,
		const HomogeneousMedium* medium)
	{
		Ray shadowRay(point, wi);
		IntersectionData boundaryIntersection = scene->traverse(shadowRay);

		if (finiteLight && boundaryIntersection.t >= distanceToLight - EPSILON)
		{
			// The sampled light lies inside the same medium and no boundary blocks it.
			return medium->transmittance(distanceToLight);
		}
		if (boundaryIntersection.t >= FLT_MAX)
		{
			return finiteLight
				? medium->transmittance(distanceToLight)
				: Colour(0.0f, 0.0f, 0.0f);
		}

		ShadingData boundary = scene->calculateShadingData(boundaryIntersection, shadowRay);
		if (boundary.bsdf->interiorMedium() != medium
			|| !boundary.bsdf->supportsMediumDirectTransmission())
		{
			return Colour(0.0f, 0.0f, 0.0f);
		}

		Colour transmittance = medium->transmittance(boundaryIntersection.t)
			* boundary.bsdf->mediumDirectTransmission(boundary);
		Vec3 boundaryPoint = shadowRay.at(boundaryIntersection.t);

		if (finiteLight)
		{
			if (!scene->visible(boundaryPoint, lightPoint))
			{
				return Colour(0.0f, 0.0f, 0.0f);
			}
		} else
		{
			Ray outsideRay(boundaryPoint + wi * EPSILON, wi);
			if (!scene->bvh->traverseVisible(outsideRay, scene->triangles, FLT_MAX))
			{
				return Colour(0.0f, 0.0f, 0.0f);
			}
		}

		return transmittance;
	}
	Colour computeMediumDirect(const Vec3& point, const Vec3& forward,
		const HomogeneousMedium* medium, Sampler* sampler)
	{
		if (scene->lights.empty())
		{
			return Colour(0.0f, 0.0f, 0.0f);
		}

		float lightPmf;
		Light* sampledLight = scene->sampleLight(sampler, lightPmf);
		ShadingData mediumVertex = {};
		mediumVertex.x = point;
		mediumVertex.wo = -forward;
		Colour emission;
		float lightPdf;
		Vec3 lightSample = sampledLight->sample(mediumVertex, sampler, emission, lightPdf);
		if (lightPmf <= 0.0f || lightPdf <= 0.0f)
		{
			return Colour(0.0f, 0.0f, 0.0f);
		}

		if (sampledLight->isArea())
		{
			Vec3 toLight = lightSample - point;
			float distanceSquared = Dot(toLight, toLight);
			if (distanceSquared <= EPSILON * EPSILON)
			{
				return Colour(0.0f, 0.0f, 0.0f);
			}
			float distance = sqrtf(distanceSquared);
			Vec3 wi = toLight / distance;
			float cosAtLight = std::max(
				0.0f, Dot(-wi, sampledLight->normal(mediumVertex, wi)));
			if (cosAtLight <= 0.0f)
			{
				return Colour(0.0f, 0.0f, 0.0f);
			}

			Colour shadowWeight = mediumShadowTransmittance(
				point, wi, distance, lightSample, true, medium);
			float phasePdf = medium->phase(forward, wi);
			float geometry = cosAtLight / distanceSquared;
			float directPdfA = lightPmf * lightPdf;
			float phasePdfA = phasePdf * geometry;
			float denominator = directPdfA + phasePdfA;
			if (denominator <= 0.0f)
			{
				return Colour(0.0f, 0.0f, 0.0f);
			}
			return emission * shadowWeight * (phasePdf * geometry / denominator);
		}

		Vec3 wi = lightSample;
		Colour shadowWeight = mediumShadowTransmittance(
			point, wi, FLT_MAX, Vec3(), false, medium);
		float phasePdf = medium->phase(forward, wi);
		float directPdfW = lightPmf * lightPdf;
		float denominator = directPdfW + phasePdf;
		if (denominator <= 0.0f)
		{
			return Colour(0.0f, 0.0f, 0.0f);
		}
		return emission * shadowWeight * (phasePdf / denominator);
	}
	Colour pathTrace(Ray& r, Colour& pathThroughput, int depth, Sampler* sampler,
		float prevPdfw = 0.0f, const HomogeneousMedium* medium = nullptr,
		Vec3 previousVertex = Vec3(), bool hasPreviousVertex = false)
	{
		IntersectionData intersection = scene->traverse(r);

		// Before shading the next surface, sample a possible collision in the current medium.
		if (medium != nullptr && intersection.t < FLT_MAX)
		{
			float mediumDistance;
			Colour mediumWeight;
			bool scattered = medium->sampleDistance(
				intersection.t, sampler, mediumDistance, mediumWeight);
			pathThroughput = pathThroughput * mediumWeight;

			if (scattered)
			{
				Vec3 scatterPoint = r.at(mediumDistance);
				Colour L = computeMediumDirect(scatterPoint, r.dir, medium, sampler)
					* pathThroughput;

				float probRR = 1.0f;
				if (depth > 3)
				{
					probRR = 0.9f;
					if (sampler->next() > probRR)
					{
						return L;
					}
					pathThroughput = pathThroughput / probRR;
				}

				float phasePdf;
				Vec3 wi = medium->samplePhase(r.dir, sampler, phasePdf);
				if (phasePdf <= 0.0f)
				{
					return L;
				}

				Ray nextRay(scatterPoint + wi * EPSILON, wi);
				return L + pathTrace(nextRay, pathThroughput, depth + 1, sampler,
					phasePdf, medium, scatterPoint, true);
			}
		}

		ShadingData shadingData = scene->calculateShadingData(intersection, r);
		if (shadingData.t >= FLT_MAX)
		{
			// hit background
			Colour L_em = scene->background->evaluate(r.dir);
			float wd = 1.0f;
			//prevPdfw == 0 means no need for MIS
			if (prevPdfw > 0.0f && scene->lights.size() > 0) {
				bool bgIsLight = false;
				for (auto l : scene->lights) {
					if (l == scene->background) { bgIsLight = true; break; }
				}
				if (bgIsLight) {//EnvironmentMap
					float pmfLights = 1.0f / scene->lights.size();
					float pdfOnLight = scene->background->PDF(shadingData, r.dir);
					float pdfwDirect = pdfOnLight * pmfLights;
					float pdfwIndirect = prevPdfw;
					if (pdfwDirect + pdfwIndirect > 0.0f) {
						wd = pdfwIndirect / (pdfwDirect + pdfwIndirect);
					}
				}
			}
			return L_em * pathThroughput * wd;
		}

		Colour L(0.0f, 0.0f, 0.0f);
		if (shadingData.bsdf->isLight())
		{
			// Use the original (unflipped) triangle normal to check if we hit from the back.
			Vec3 originalNormal = scene->triangles[intersection.ID].gNormal();
			if (Dot(shadingData.wo, originalNormal) < 0)
			{
				// Ray hit the back side of the area light — no emission
				return Colour(0.0f, 0.0f, 0.0f);
			}
			//hit light
			Colour L_em = shadingData.bsdf->emit(shadingData, shadingData.wo);
			float wd = 1.0f;
			if (prevPdfw > 0.0f && scene->lights.size() > 0) {
				float pmfLights = 1.0f / scene->lights.size();
				float area = scene->triangles[intersection.ID].area;
				float pdfPosOnLight = 1.0f / area;
				float pdfADirect = pdfPosOnLight * pmfLights;
				float rSq = hasPreviousVertex
					? Dot(shadingData.x - previousVertex, shadingData.x - previousVertex)
					: shadingData.t * shadingData.t;
				float cosThetaLight = std::max(0.0f, Dot(shadingData.wo, shadingData.gNormal));
				float pdfAIndirect = 0.0f;
				if (rSq > 1e-6f) {
					pdfAIndirect = prevPdfw * cosThetaLight / rSq;
				}
				wd = pdfAIndirect / (pdfADirect + pdfAIndirect);				
			}
			L = L + L_em * pathThroughput * wd;
			return L;
		}
		L = computeDirect(shadingData, sampler) * pathThroughput;
		float probRR = 1.f;//survive rate			
		if (depth > 3) {
			probRR = 0.9f;
			if (sampler->next() > probRR)
			{
				return L;
			}
		}
		Colour  bsdfVal;
		float pdf;
		Vec3 wi = shadingData.bsdf->sample(shadingData, sampler, bsdfVal, pdf);

		if (pdf > 0.0f)
		{
			pathThroughput = pathThroughput * bsdfVal * fabsf(Dot(wi, shadingData.sNormal)) / (pdf * probRR);

			const HomogeneousMedium* nextMedium = medium;
			const HomogeneousMedium* boundaryMedium = shadingData.bsdf->interiorMedium();
			bool transmitted = false;
			if (boundaryMedium != nullptr)
			{
				float woSide = Dot(shadingData.wo, shadingData.gNormal);
				float wiSide = Dot(wi, shadingData.gNormal);
				transmitted = woSide * wiSide < 0.0f;
				if (transmitted)
				{
					// This baseline supports one non-nested interior medium.
					nextMedium = (medium == boundaryMedium) ? nullptr : boundaryMedium;
				}
			}

			Ray nextRay(shadingData.x + (wi * EPSILON), wi);
			float nextPrevPdfw = pdf;
			Vec3 nextPreviousVertex = shadingData.x;
			bool nextHasPreviousVertex = true;
			if (shadingData.bsdf->isPureSpecular())
			{
				bool preserveMediumMIS = transmitted
					&& shadingData.bsdf->supportsStraightTransmission()
					&& prevPdfw > 0.0f && hasPreviousVertex;
				if (preserveMediumMIS)
				{
					nextPrevPdfw = prevPdfw;
					nextPreviousVertex = previousVertex;
					nextHasPreviousVertex = true;
				} else
				{
					nextPrevPdfw = 0.0f;
					nextHasPreviousVertex = false;
				}
			}
			L = L + pathTrace(nextRay, pathThroughput, depth+1, sampler,
				nextPrevPdfw, nextMedium, nextPreviousVertex, nextHasPreviousVertex);
		}
		return L;
	}
	void workerLoop(int threadId)
	{
		int myGeneration = 0;
		while (true)
		{
			std::unique_lock<std::mutex> lock(syncMutex);
			startCV.wait(lock, [this, myGeneration]() {
				return exitThreads || generation > myGeneration;
			});

			if (exitThreads) return;

			myGeneration = generation;
			lock.unlock();

			while (true)
			{
				unsigned int tileIndex = nextTile.fetch_add(1);
				if (tileIndex >= totalTiles)
				{
					break;
				}

				unsigned int tileX = tileIndex % numTilesX;
				unsigned int tileY = tileIndex / numTilesX;

				unsigned int startX = tileX * tileSize;
				unsigned int startY = tileY * tileSize;
				unsigned int endX = std::min(startX + tileSize, film->width);
				unsigned int endY = std::min(startY + tileSize, film->height);

				for (unsigned int y = startY; y < endY; y++)
				{
					for (unsigned int x = startX; x < endX; x++)
					{
						float px = x + 0.5f;
						float py = y + 0.5f;

						Ray ray = scene->camera.generateRay(px, py);						
						Colour pt(1.0f, 1.0f, 1.0f);
						Colour col = pathTrace(ray, pt, 0, &samplers[threadId]);
						film->splat(px, py, col);
					}
				}
			}

			lock.lock();
			activeWorkers--;
			if (activeWorkers == 0)
			{
				doneCV.notify_all();
			}
		}
	}

	void render()
	{
		film->incrementSPP();

		numTilesX = (film->width + tileSize - 1) / tileSize;
		unsigned int numTilesY = (film->height + tileSize - 1) / tileSize;
		totalTiles = numTilesX * numTilesY;
		nextTile = 0;

		{
			std::unique_lock<std::mutex> lock(syncMutex);
			activeWorkers = numProcs;
			generation++;
		}
		startCV.notify_all();

		{
			std::unique_lock<std::mutex> lock(syncMutex);
			doneCV.wait(lock, [this]() { return activeWorkers == 0; });
		}

		for (unsigned int y = 0; y < film->height; y++)
		{
			for (unsigned int x = 0; x < film->width; x++)
			{
				unsigned char r, g, b;
				film->tonemap(x, y, r, g, b);
				canvas->draw(x, y, r, g, b);
			}
		}
	}
	int getSPP()
	{
		return film->SPP;
	}
	void saveHDR(std::string filename)
	{
		film->save(filename);
	}
	void savePNG(std::string filename)
	{
		stbi_write_png(filename.c_str(), canvas->getWidth(), canvas->getHeight(), 3, canvas->getBackBuffer(), canvas->getWidth() * 3);
	}
};

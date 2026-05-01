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
#include <functional>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include<OpenImageDenoise/oidn.hpp>

class VPL
{
public:
	ShadingData shadingData;
	Colour Le;
};

class RayTracer
{
public:
	Scene* scene = nullptr;
	GamesEngineeringBase::Window* canvas = nullptr;
	Film* film = nullptr;
	MTRandom *samplers = nullptr;
	std::thread **threads = nullptr;
	int numProcs = 0;
	std::vector<VPL> vpls;
	
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

	// for denoising
	//std::vector<Colour> albedoBuffer;
	//std::vector<Colour> normalBuffer;

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
		//film->init((unsigned int)scene->camera.width, (unsigned int)scene->camera.height, new BoxFilter());
		film->init((unsigned int)scene->camera.width, (unsigned int)scene->camera.height, new MitchellNetravaliFilter());
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		numProcs = sysInfo.dwNumberOfProcessors;
		
		if (threads == nullptr) {
			threads = new std::thread*[numProcs];
			samplers = new MTRandom[numProcs];
			exitThreads = false;
			for (int i = 0; i < numProcs; i++) {
				threads[i] = new std::thread(&RayTracer::workerLoop, this, i);
			}
		}

		//albedoBuffer.resize(scene->camera.width * scene->camera.height);
		//normalBuffer.resize(scene->camera.width * scene->camera.height);

		clear();
	}
	void clear()
	{
		film->clear();
	}
	Colour computeDirect(ShadingData shadingData, Sampler* sampler)
	{
		// If surface is specular we cannot computing direct lighting
		if (shadingData.bsdf->isPureSpecular() == true)
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
	Colour pathTrace(Ray& r, Colour& pathThroughput, int depth, Sampler* sampler, float prevPdfw = 0.0f)
	{
		IntersectionData intersection = scene->traverse(r);
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
				float rSq = shadingData.t * shadingData.t;
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
			Ray nextRay(shadingData.x + (wi * EPSILON), wi);
			L = L + pathTrace(nextRay, pathThroughput, depth+1, sampler, shadingData.bsdf->isPureSpecular() ? 0.0f : pdf);
		}
		return L;
	}
	void splatToCamera(Vec3 p, Vec3 n, Colour col)
	{
		// FOV visible
		float x, y;
		if (!scene->camera.projectOntoCamera(p, x, y)) return;
		// occlusion visible
		if (!scene->visible(p, scene->camera.origin)) return;
		//surface faces the camera
		Vec3 toCamera = scene->camera.origin - p;
		if (Dot(n, toCamera) <= 0.0f) return;

		Vec3 cameraToP = -toCamera.normalize();
		float cosTheta = Dot(scene->camera.viewDirection, cameraToP);
		float cos2Theta = cosTheta * cosTheta;
		float cos4Theta = cos2Theta * cos2Theta;
		float We = 1.0f / (scene->camera.Afilm * cos4Theta);
		float G_term = Dot(n, toCamera.normalize()) * cosTheta / Dot(toCamera, toCamera);
		Colour splatCol = col * We * G_term;
		film->splat(x, y, splatCol);
	}
	void lightTrace(Sampler* sampler)
	{
		float pmfLights;
		Light* light = scene->sampleLight(sampler, pmfLights);
		float pdfPosition, pdfDirection;
		Vec3 p = light->samplePositionFromLight(sampler, pdfPosition);
		Vec3 wi = light->sampleDirectionFromLight(sampler, pdfDirection);
		Colour Le = light->evaluate(-wi);
		ShadingData sd;//dummy
		Vec3 lightNormal = light->normal(sd, wi);

		if (light->isArea())
		{
			Colour colCamera = Le / (pdfPosition * pmfLights);
			splatToCamera(p, lightNormal, colCamera);
		}

		Colour pathThroughput = Le * fabsf(Dot(wi, lightNormal)) / (pdfPosition * pdfDirection * pmfLights);
		Ray r(p + wi * EPSILON, wi);
		lightTracePath(r, pathThroughput, Le, sampler);

	}
	void lightTracePath(Ray& r, Colour pathThroughput, Colour Le, Sampler* sampler, int depth = 0)
	{
		IntersectionData intersection = scene->traverse(r);
		ShadingData shadingData = scene->calculateShadingData(intersection, r);
		if (shadingData.t >= FLT_MAX) return;
		if (shadingData.bsdf->isLight()) return; // hit a light, stop

		if (!shadingData.bsdf->isPureSpecular())
		{
			// Direction from hit point to camera
			Vec3 toCamera = (scene->camera.origin - shadingData.x).normalize();
			Colour bsdfVal = shadingData.bsdf->evaluate(shadingData, toCamera);
			// col = pathThroughput * bsdf(wo, wCam)
			splatToCamera(shadingData.x, shadingData.sNormal, pathThroughput * bsdfVal);
		}

		float probRR = 1.0f;
		if (depth > 3)
		{
			probRR = 0.9f;
			if (sampler->next() > probRR) return;
		}

		Colour bsdfSampleVal;
		float pdf;
		Vec3 wi = shadingData.bsdf->sample(shadingData, sampler, bsdfSampleVal, pdf);
		if (pdf > 0.0f)//to avoid nan currently, but should check in sample function
		{
			pathThroughput = pathThroughput * bsdfSampleVal * fabsf(Dot(wi, shadingData.sNormal)) / (pdf * probRR);
			Ray nextRay(shadingData.x + wi * EPSILON, wi);
			lightTracePath(nextRay, pathThroughput, Le, sampler, depth + 1);
		}
	}
	Colour direct(Ray& r, Sampler* sampler)
	{
		IntersectionData intersection = scene->traverse(r);
		ShadingData shadingData = scene->calculateShadingData(intersection, r);
		if (shadingData.t < FLT_MAX)
		{
			if (shadingData.bsdf->isLight())
			{
				Vec3 originalNormal = scene->triangles[intersection.ID].gNormal();
				if (Dot(shadingData.wo, originalNormal) < 0)
				{
					return Colour(0.0f, 0.0f, 0.0f);
				}
				return shadingData.bsdf->emit(shadingData, shadingData.wo);
			}
			return computeDirect(shadingData, sampler);
		}
		return scene->background->evaluate(r.dir);

		// Compute direct lighting for an image sampler here
	}
	Colour albedo(Ray& r)
	{
		IntersectionData intersection = scene->traverse(r);
		ShadingData shadingData = scene->calculateShadingData(intersection, r);
		if (shadingData.t < FLT_MAX)
		{
			if (shadingData.bsdf->isLight())
			{
				return shadingData.bsdf->emit(shadingData, shadingData.wo);
			}
			return shadingData.bsdf->evaluate(shadingData, Vec3(0, 1, 0));
		}
		return scene->background->evaluate(r.dir);
	}
	Colour viewNormals(Ray& r)
	{
		IntersectionData intersection = scene->traverse(r);
		if (intersection.t < FLT_MAX)
		{
			ShadingData shadingData = scene->calculateShadingData(intersection, r);
			return Colour(fabsf(shadingData.sNormal.x), fabsf(shadingData.sNormal.y), fabsf(shadingData.sNormal.z));
		}
		return Colour(0.0f, 0.0f, 0.0f);
	}
	void generateVPLs(int numPaths)
	{
		vpls.clear();
		if (scene->lights.size() == 0) return;

		for (int i = 0; i < numPaths; i++)
		{
			float pmfLights;
			Light* light = scene->sampleLight(&samplers[0], pmfLights);
			float pdfPosition, pdfDirection;
			Vec3 p = light->samplePositionFromLight(&samplers[0], pdfPosition);
			Vec3 wi = light->sampleDirectionFromLight(&samplers[0], pdfDirection);
			Colour Le = light->evaluate(-wi);
			ShadingData dummy;
			Vec3 lightNormal = light->normal(dummy, wi);
			
			//generate vpl on the light
			VPL lightVPL;
			lightVPL.shadingData = ShadingData(p, lightNormal);
			lightVPL.Le = Le / (pdfPosition * pmfLights * numPaths);
			vpls.push_back(lightVPL);

			Colour pathThroughput = lightVPL.Le * Dot(wi, lightNormal) / pdfDirection;
			Ray r(p + wi * EPSILON, wi);
			traceVPL(r, pathThroughput, &samplers[0], 0);
		}
	}
	void traceVPL(Ray& r, Colour pathThroughput, Sampler* sampler, int depth)
	{
		IntersectionData intersection = scene->traverse(r);
		ShadingData shadingData = scene->calculateShadingData(intersection, r);
		if (shadingData.t >= FLT_MAX) return;
		if (shadingData.bsdf->isLight()) return;

		if (!shadingData.bsdf->isPureSpecular())
		{
			VPL vpl;
			vpl.shadingData = shadingData;
			vpl.Le = pathThroughput;
			vpls.push_back(vpl);
		}

		float probRR = 1.0f;
		if (depth > 3)
		{
			probRR = 0.9f;
			if (sampler->next() > probRR) return;
		}

		Colour bsdfSampleVal;
		float pdf;
		Vec3 wi = shadingData.bsdf->sample(shadingData, sampler, bsdfSampleVal, pdf);
		if (pdf > 0.0f)
		{
			Colour nextPathThroughput = pathThroughput * bsdfSampleVal * fabsf(Dot(wi, shadingData.sNormal)) / (pdf * probRR);
			Ray nextRay(shadingData.x + wi * EPSILON, wi);
			traceVPL(nextRay, nextPathThroughput, sampler, depth + 1);
		}
	}
	Colour instantRadiosity(Ray& r, Sampler* sampler, int depth = 0)
	{
		IntersectionData intersection = scene->traverse(r);
		ShadingData shadingData = scene->calculateShadingData(intersection, r);
		if (shadingData.t >= FLT_MAX)
		{
			return scene->background->evaluate(r.dir);
		}
		if (shadingData.bsdf->isLight())
		{
			Vec3 originalNormal = scene->triangles[intersection.ID].gNormal();
			if (Dot(shadingData.wo, originalNormal) < 0) return Colour(0.0f, 0.0f, 0.0f);
			return shadingData.bsdf->emit(shadingData, shadingData.wo);
		}
		Colour L(0.0f, 0.0f, 0.0f);
		if (shadingData.bsdf->isPureSpecular())
		{
			if (depth > 5) return L; // to avoid bouncing between specular surfaces
			Colour bsdfSampleVal;
			float pdf;
			Vec3 wi = shadingData.bsdf->sample(shadingData, sampler, bsdfSampleVal, pdf);
			if (pdf > 0.0f)
			{
				Ray nextRay(shadingData.x + wi * EPSILON, wi);
				L = instantRadiosity(nextRay, sampler, depth + 1) * bsdfSampleVal * fabsf(Dot(wi, shadingData.sNormal)) / pdf;
			}
			return L;
		}

		for (int i = 0; i < vpls.size(); i++)
		{
			VPL vpl = vpls[i];
			if (!scene->visible(shadingData.x, vpl.shadingData.x)) {
				continue;
			}
			Vec3 dir = vpl.shadingData.x - shadingData.x;
			float distSq = Dot(dir, dir);
			dir = dir.normalize();
			float cosTheta1 = Dot(shadingData.sNormal, dir);
			float cosTheta2 = Dot(vpl.shadingData.sNormal, -dir);
			if (cosTheta1 < 1e-6f || cosTheta2 < 1e-6f)
			{
				continue;
			}
			Colour fr = shadingData.bsdf->evaluate(shadingData, dir);
			Colour vpl_fr(1.0f, 1.0f, 1.0f); //for vpl on light source
			if (vpl.shadingData.bsdf != nullptr)
			{
				vpl_fr = vpl.shadingData.bsdf->evaluate(vpl.shadingData, -dir);
			}
					
			float G_term = cosTheta1 * cosTheta2 / distSq;
			L = L + vpl_fr * G_term * fr * vpl.Le;
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

						// === Instant Radiosity mode ===
						//Ray ray = scene->camera.generateRay(px, py);						
						//Colour col = instantRadiosity(ray, &samplers[threadId]);
						//film->splat(px, py, col);

						// === Light Tracing mode ===
						//lightTrace(&samplers[threadId]);

						//=== Path Tracing mode ===
						Ray ray = scene->camera.generateRay(px, py);						
						Colour pt(1.0f, 1.0f, 1.0f);
						Colour col = pathTrace(ray, pt, 0, &samplers[threadId]);
						film->splat(px, py, col);

						// === for denoising ===
						//int idx = y * film->width + x;
						//albedoBuffer[idx] = albedo(ray);
						//normalBuffer[idx] = viewNormals(ray);
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

		generateVPLs(1024);

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
	//void denoise() {
	//	int width = film->width;
	//	int height = film->height;
	//	int numPixels = width * height;
	//	size_t bufferSize = numPixels * 3 * sizeof(float);
	//	float invSPP = 1.0f / (float)film->SPP;
	//	oidn::DeviceRef device = oidn::newDevice();
	//	device.commit();
	//	oidn::BufferRef colorBuf = device.newBuffer(bufferSize);
	//	oidn::BufferRef albedoBuf = device.newBuffer(bufferSize);
	//	oidn::BufferRef normalBuf = device.newBuffer(bufferSize);
	//	oidn::BufferRef outputBuf = device.newBuffer(bufferSize);
	//	oidn::FilterRef filter = device.newFilter("RT");

	//	filter.setImage("color", colorBuf, oidn::Format::Float3, width, height);
	//	filter.setImage("albedo", albedoBuf, oidn::Format::Float3, width, height);
	//	filter.setImage("normal", normalBuf, oidn::Format::Float3, width, height);
	//	filter.setImage("output", outputBuf, oidn::Format::Float3, width, height);

	//	filter.set("hdr", true);
	//	filter.commit();

	//	// fill the input image buffers copy from film, dividing by SPP
	//	float* colorPtr = (float*)colorBuf.getData();
	//	float* a = (float*)albedoBuf.getData();
	//	float* n = (float*)normalBuf.getData();
	//	for (int i = 0; i < numPixels; i++) {
	//		colorPtr[3 * i + 0] = film->film[i].r * invSPP;
	//		colorPtr[3 * i + 1] = film->film[i].g * invSPP;
	//		colorPtr[3 * i + 2] = film->film[i].b * invSPP;
	//		a[3 * i + 0] = albedoBuffer[i].r;
	//		a[3 * i + 1] = albedoBuffer[i].g;
	//		a[3 * i + 2] = albedoBuffer[i].b;
	//		n[3 * i + 0] = normalBuffer[i].r;
	//		n[3 * i + 1] = normalBuffer[i].g;
	//		n[3 * i + 2] = normalBuffer[i].b;
	//	}
	//	filter.execute();

	//	float* out = (float*)outputBuf.getData();

	//	for (int i = 0; i < numPixels; i++)
	//	{
	//		film->film[i].r = out[3 * i + 0] * film->SPP;
	//		film->film[i].g = out[3 * i + 1] * film->SPP;
	//		film->film[i].b = out[3 * i + 2] * film->SPP;
	//	}
	//}
	void redrawFilmToCanvas()
	{
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
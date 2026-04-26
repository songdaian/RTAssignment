#pragma once

#include "Core.h"
#include "Sampling.h"

class Ray
{
public:
	Vec3 o;
	Vec3 dir;
	Vec3 invDir;
	Ray()
	{
	}
	Ray(Vec3 _o, Vec3 _d)
	{
		init(_o, _d);
	}
	void init(Vec3 _o, Vec3 _d)
	{
		o = _o;
		dir = _d;
		invDir = Vec3(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z);
	}
	Vec3 at(const float t) const
	{
		return (o + (dir * t));
	}
};
#define EPSILON 0.001f

class Plane
{
public:
	Vec3 n;
	float d;
	void init(Vec3& _n, float _d)
	{
		n = _n;
		d = _d;
	}
	bool rayIntersect(Ray& r, float& t)
	{
		float denom = Dot(n, r.dir);
		if (fabs(denom) < EPSILON) { return false; }
		t = -(d + Dot(n, r.o)) / denom;
		if (t < 0) { return false; }
		return true;
	}
};


class Triangle
{
public:
	Vertex vertices[3];
	Vec3 e1; // Edge 1
	Vec3 e2; // Edge 2
	Vec3 n; // Geometric Normal
	float area; // Triangle area
	float d; // For ray triangle if needed
	unsigned int materialIndex;
	void init(Vertex v0, Vertex v1, Vertex v2, unsigned int _materialIndex)
	{
		materialIndex = _materialIndex;
		vertices[0] = v0;
		vertices[1] = v1;
		vertices[2] = v2;
		e1 = vertices[2].p - vertices[1].p;
		e2 = vertices[0].p - vertices[2].p;
		n = e1.cross(e2).normalize();
		area = e1.cross(e2).length() * 0.5f;
		d = -Dot(n, vertices[0].p);
	}
	Vec3 centre() const
	{
		return (vertices[0].p + vertices[1].p + vertices[2].p) / 3.0f;
	}
	// Add code here
	bool rayIntersect(const Ray& r, float& t, float& alpha, float& beta) const
	{
		////Method1: start with ray-plane intersection 
		//float denom = Dot(n, r.dir);
		//if (denom == 0) { return false; }
		//t = -(d + Dot(n, r.o)) / denom;
		//if (t < 0) { return false; }
		//Vec3 p = r.at(t);
		//float invArea = 1.0f / Dot(e1.cross(e2), n);
		//alpha = Dot(e1.cross(p - vertices[1].p), n) * invArea;
		//if (alpha < 0 || alpha > 1.0f) { return false; }
		//beta = Dot(e2.cross(p - vertices[2].p), n) * invArea;
		//if (beta < 0 || (alpha + beta) > 1.0f) { return false; }
		//return true;

		//Method2:Moller-Trumbore
		Vec3 T = r.o - vertices[2].p;
		Vec3 P = Cross(r.dir, e2);
		Vec3 Q = Cross(T, e1);
		float denom = Dot(e1, P);
		if (fabs(denom) < 1e-7f) { return false; }
		float invdet = 1 / denom;
		alpha = Dot(r.dir, Q) * invdet;
		if (alpha < 0 || alpha > 1) { return false; }
		beta = - Dot(T, P) * invdet;
		if (beta < 0 || alpha + beta > 1) { return false; }
		t = Dot(e2, Q) * invdet;
		if (t < 0) { return false; }
		return true;


	}
	void interpolateAttributes(const float alpha, const float beta, const float gamma, Vec3& interpolatedNormal, float& interpolatedU, float& interpolatedV) const
	{
		interpolatedNormal = vertices[0].normal * alpha + vertices[1].normal * beta + vertices[2].normal * gamma;
		interpolatedNormal = interpolatedNormal.normalize();
		interpolatedU = vertices[0].u * alpha + vertices[1].u * beta + vertices[2].u * gamma;
		interpolatedV = vertices[0].v * alpha + vertices[1].v * beta + vertices[2].v * gamma;
	}
	// Add code here
	Vec3 sample(Sampler* sampler, float& pdf)
	{
		float r1 = sampler->next();
		float r2 = sampler->next();
		float alpha = 1 - sqrt(r1);
		float beta = r2 * sqrt(r1);
		pdf = 1.f / area;
		return vertices[0].p * alpha + vertices[1].p * beta + vertices[2].p * (1-alpha-beta);
	}
	Vec3 gNormal()
	{
		return (n * (Dot(vertices[0].normal, n) > 0 ? 1.0f : -1.0f));
	}
};

class AABB
{
public:
	Vec3 max;
	Vec3 min;
	AABB()
	{
		reset();
	}
	void reset()
	{
		max = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		min = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	}
	void extend(const Vec3 p)
	{
		max = Max(max, p);
		min = Min(min, p);
	}
	bool rayAABB(const Ray& r, float& t)
	{
		Vec3 Tmin = (min - r.o) * r.invDir;
		Vec3 Tmax = (max - r.o) * r.invDir;
		Vec3 Tentry = Min(Tmin, Tmax);
		Vec3 Texit = Max(Tmin, Tmax);
		float tentry = std::max(Tentry.x, std::max(Tentry.y, Tentry.z));
		float texit = std::min(Texit.x, std::min(Texit.y, Texit.z));
		if (tentry > texit || texit < 0)
			return false;
		
		t = tentry;
		return true;
	}

	bool rayAABB(const Ray& r)
	{
		Vec3 Tmin = (min - r.o) * r.invDir;
		Vec3 Tmax = (max - r.o) * r.invDir;
		Vec3 Tentry = Min(Tmin, Tmax);
		Vec3 Texit = Max(Tmin, Tmax);
		float tentry = std::max(Tentry.x, std::max(Tentry.y, Tentry.z));
		float texit = std::min(Texit.x, std::min(Texit.y, Texit.z));
		return (tentry <= texit && texit >= 0);
	}

	float area()
	{
		Vec3 size = max - min;
		return ((size.x * size.y) + (size.y * size.z) + (size.x * size.z)) * 2.0f;
	}
};

class Sphere
{
public:
	Vec3 centre;
	float radius;
	void init(Vec3& _centre, float _radius)
	{
		centre = _centre;
		radius = _radius;
	}
	// Add code here
	bool rayIntersect(Ray& r, float& t)
	{
		Vec3 l = r.o - centre;
		float b = Dot(l, r.dir);
		float c = Dot(l, l) - radius * radius;
		float dis = b * b - c;
		if (dis < 0) {
			return false;
		}
		float sqrtDis = sqrt(dis);
		if (-b - sqrtDis > 0) {
			t = -b - sqrtDis;
			return true;
		}
		if (-b + sqrtDis > 0) {
			t = -b + sqrtDis;
			return true;
		}
		return false;
	}
};

struct IntersectionData
{
	unsigned int ID;
	float t;
	float alpha;
	float beta;
	float gamma;
};

#define MAXNODE_TRIANGLES 8
#define TRAVERSE_COST 1.0f
#define TRIANGLE_COST 2.0f
#define BUILD_BINS 16

struct Bin { 
	AABB bounds;
	int count = 0; 
};

class BVHNode
{
public:
	AABB bounds;
	BVHNode* r;
	BVHNode* l;
	// This can store an offset and number of triangles in a global triangle list for example
	// But you can store this however you want!
	// unsigned int offset;
	// unsigned char num;
	unsigned int offset;
	unsigned int num; // 0 if current Node is not a leaf node
	
	BVHNode()
	{
		r = NULL;
		l = NULL;
		offset = 0;
		num = 0;
	}
	// Note there are several options for how to implement the build method. Update this as required
	float axisComponent(const Vec3& v, int axis) const {
		if (axis == 0) return v.x;
		if (axis == 1) return v.y;
		return v.z;
	}

	AABB calcFullBounds(std::vector<Triangle>& tris, int start, int count) {
		AABB b;
		for (int i = start; i < start + count; i++) {
			b.extend(tris[i].vertices[0].p);
			b.extend(tris[i].vertices[1].p);
			b.extend(tris[i].vertices[2].p);
		}
		return b;
	}

	AABB calcCentroidBounds(std::vector<Triangle>& triangles, int start, int count) {
		AABB b;
		for (int i = start; i < start + count; i++) {
			b.extend(triangles[i].centre());
		}
		return b;
	}

	void auxBuild(std::vector<Triangle>& triangles, int start, int count) {
		offset = start;
		num = count; // will be set to 0 at the end if still need to split
		bounds = calcFullBounds(triangles, start, count);
		if (count == 1) return; //end condition: 1 triangle

		//Use centroid bounds to split!
		AABB cBounds = calcCentroidBounds(triangles, start, count);
		Vec3 lens = cBounds.max - cBounds.min;

		float C_leaf = count * TRIANGLE_COST;
		float minCost = C_leaf;
		int ansAxis = -1;
		int ansSplitIdx = -1;

		for (int axis = 0; axis < 3; axis++) {
			if (axisComponent(lens, axis) < EPSILON) continue; //Avoid div 0
			std::vector<Bin> bins(BUILD_BINS);
			float invBinLen = BUILD_BINS / axisComponent(lens, axis);
			// for each triangle, culculate bin index and update that bin
			for (int j = start; j < start + count; j++) {
				int binIdx = std::floor((axisComponent(triangles[j].centre(), axis) - axisComponent(cBounds.min,axis)) * invBinLen);
				binIdx = std::max(0, std::min(binIdx, BUILD_BINS - 1));//clamp to avoid exception caused by floating point precision error
				bins[binIdx].count++;
				bins[binIdx].bounds.extend(triangles[j].vertices[0].p);
				bins[binIdx].bounds.extend(triangles[j].vertices[1].p);
				bins[binIdx].bounds.extend(triangles[j].vertices[2].p);
			}
			
			// Hard to shrink box, so pre-calculate and store j-th right box's(from bin j+1 to BUILD_BINS-1) info in rightAreas[j]
			std::vector<float> rightAreas(BUILD_BINS - 1, 0);
			std::vector<int> rightCounts(BUILD_BINS - 1, 0);
			AABB rightBox;
			int rightCount = 0;
			for (int j = BUILD_BINS - 2; j >= 0; j--) {
				rightCount += bins[j+1].count;
				rightCounts[j] = rightCount;
				if (bins[j+1].count > 0) {
					rightBox.extend(bins[j+1].bounds.min);
					rightBox.extend(bins[j+1].bounds.max);
				}
				if (rightCount > 0) {
					rightAreas[j] = rightBox.area();
				}
			}

			AABB leftBox;
			int leftCount = 0;
			float denom = TRIANGLE_COST / bounds.area();
			for (int j = 0; j < BUILD_BINS - 1; j++) {
				leftCount += bins[j].count;
				if (leftCount == 0 || rightCounts[j] == 0) continue;
				if (bins[j].count > 0) {
					leftBox.extend(bins[j].bounds.min);
					leftBox.extend(bins[j].bounds.max);
				}
				float cost = TRAVERSE_COST + (leftCount * leftBox.area() + rightCounts[j] * rightAreas[j]) * denom;
				if (cost < minCost) {
					minCost = cost;
					ansAxis = axis;
					ansSplitIdx = j;
				}
			}
		}

		if (ansAxis == -1) return; //end condition: no need for split

		float invBinLen = BUILD_BINS / axisComponent(lens, ansAxis);
		int left = start;
		int right = start + count - 1;		
		while (left <= right) {
			while (left <= right) {
				int binIdx = std::floor((axisComponent(triangles[left].centre(), ansAxis) - axisComponent(cBounds.min, ansAxis)) * invBinLen);
				binIdx = std::max(0, std::min(binIdx, BUILD_BINS - 1));
				if (binIdx <= ansSplitIdx) left++;
				else break;
			}			
			while (left <= right) {
				int binIdx = std::floor((axisComponent(triangles[right].centre(), ansAxis) - axisComponent(cBounds.min, ansAxis)) * invBinLen);
				binIdx = std::max(0, std::min(binIdx, BUILD_BINS - 1));
				if (binIdx > ansSplitIdx) right--;
				else break;
			}
			if (left < right) {
				std::swap(triangles[left], triangles[right]);
				left++;
				right--;
			}
		}
		int sep = left;
		if (sep == start || sep == start + count) return;
		num = 0;
		l = new BVHNode();
		r = new BVHNode();
		l->auxBuild(triangles, start, sep - start);
		r->auxBuild(triangles, sep, start + count - sep);
	}

	void build(std::vector<Triangle>& inputTriangles)
	{
		// Add BVH building code here
		if (inputTriangles.empty()) return;
		auxBuild(inputTriangles, 0, inputTriangles.size());
	}
	
	void traverse(const Ray& ray, const std::vector<Triangle>& triangles, IntersectionData& intersection)
	{
		// Add BVH Traversal code here
		float t;
		if (!bounds.rayAABB(ray, t)) return;
		if (t >= intersection.t) return;
		if (num > 0) {
			// leaf
			for (int i = 0; i < num; i++) {
				float alpha, beta;
				if (triangles[offset + i].rayIntersect(ray, t, alpha, beta)) {
					if (t < intersection.t) {
						intersection.t = t;
						intersection.alpha = alpha;
						intersection.beta = beta;
						intersection.gamma = 1.0f - alpha - beta;
						intersection.ID = offset + i;
					}
				}
			}
		} else {
			if (l) l->traverse(ray, triangles, intersection);
			if (r) r->traverse(ray, triangles, intersection);
		}
	}
	
	IntersectionData traverse(const Ray& ray, const std::vector<Triangle>& triangles)
	{
		IntersectionData intersection;
		intersection.t = FLT_MAX;
		traverse(ray, triangles, intersection);
		return intersection;
	}
	
	bool traverseVisible(const Ray& ray, const std::vector<Triangle>& triangles, const float maxT)
	{
		// Add visibility code here
		float t;
		if (!bounds.rayAABB(ray, t)) return true;
		if (t >= maxT) return true;

		if (num > 0) {
			for (unsigned int i = 0; i < num; i++) {
				float alpha, beta;
				if (triangles[offset + i].rayIntersect(ray, t, alpha, beta)) {
					if (t < maxT) return false; //hit any nearer triangle, return false.
				}
			}
		} else {
			if (l && !l->traverseVisible(ray, triangles, maxT)) return false;
			if (r && !r->traverseVisible(ray, triangles, maxT)) return false;
		}
		return true;
	}
};

#include "GEMLoader.h"
#include "Renderer.h"
#include "SceneLoader.h"
#define NOMINMAX
#include "GamesEngineeringBase.h"

namespace RenderSettings
{
	// Edit these values, then rebuild and run.
	const char* sceneName = "volumetric-cornell";
	const char* outputFilename = "pathTracing.hdr";
	constexpr unsigned int samplesPerPixel = 8192;
	// Warm white marble tuned for the scale of the cube in volumetric-cornell.
	// Moderate scattering keeps the object milky without excessively long,
	// noisy random walks.
	const Colour sigmaS(4.0f, 4.5f, 5.0f);
	const Colour sigmaA(0.06f, 0.10f, 0.18f);
	constexpr float g = 0.0f;
}

static void applyVolumeSettings(Scene* scene)
{
	for (BSDF* material : scene->materials)
	{
		HomogeneousMediumBSDF* volume = dynamic_cast<HomogeneousMediumBSDF*>(material);
		if (volume)
		{
			volume->medium = HomogeneousMedium(
				RenderSettings::sigmaA, RenderSettings::sigmaS, RenderSettings::g);
		}
	}
}

int main()
{
	const std::string filename = RenderSettings::outputFilename;
	Scene* scene = loadScene(RenderSettings::sceneName);
	applyVolumeSettings(scene);
	GamesEngineeringBase::Window canvas;
	canvas.create((unsigned int)scene->camera.width, (unsigned int)scene->camera.height, "Tracer", false);
	RayTracer rt;
	rt.init(scene, &canvas);
	bool running = true;
	GamesEngineeringBase::Timer timer;
	while (running)
	{
		canvas.checkInput();
		canvas.clear();
		if (canvas.keyPressed(VK_ESCAPE))
		{
			break;
		}
		if (canvas.keyPressed('W'))
		{
			viewcamera.forward();
			rt.clear();
		}
		if (canvas.keyPressed('S'))
		{
			viewcamera.back();
			rt.clear();
		}
		if (canvas.keyPressed('A'))
		{
			viewcamera.left();
			rt.clear();
		}
		if (canvas.keyPressed('D'))
		{
			viewcamera.right();
			rt.clear();
		}
		if (canvas.keyPressed('E'))
		{
			viewcamera.flyUp();
			rt.clear();
		}
		if (canvas.keyPressed('Q'))
		{
			viewcamera.flyDown();
			rt.clear();
		}
		// Time how long a render call takes
		timer.reset();
		rt.render();
		float t = timer.dt();
		// Write
		std::cout << t << std::endl;
		if (canvas.keyPressed('P'))
		{
			rt.saveHDR(filename);
		}
		if (canvas.keyPressed('L'))
		{
			size_t pos = filename.find_last_of('.');
			std::string ldrFilename = filename.substr(0, pos) + ".png";
			rt.savePNG(ldrFilename);
		}
		if (RenderSettings::samplesPerPixel == rt.getSPP())
		{
			size_t pos = filename.find_last_of('.');
			std::string ldrFilename = filename.substr(0, pos) + ".png";
			rt.savePNG(ldrFilename);
			rt.saveHDR(filename);
			break;
		}
		canvas.present();
	}
	return 0;
}

#if defined(__APPLE__)

#include "GamesEngineeringBase.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include <cstdlib>
#include <vector>

using GamesEngineeringBase::MouseButton;
using GamesEngineeringBase::MouseButtonState;
using GamesEngineeringBase::MouseDown;
using GamesEngineeringBase::MouseLeft;
using GamesEngineeringBase::MouseMiddle;
using GamesEngineeringBase::MousePressed;
using GamesEngineeringBase::MouseRight;
using GamesEngineeringBase::MouseUp;
using GamesEngineeringBase::Window;

namespace
{
	struct MacWindowImpl
	{
		NSWindow* window = nil;
		MTKView* view = nil;
		id<MTLDevice> device = nil;
		id<MTLCommandQueue> commandQueue = nil;
		id<MTLRenderPipelineState> pipeline = nil;
		id<MTLTexture> texture = nil;
		std::vector<unsigned char> rgba;
	};

	int keyFromEvent(NSEvent* event)
	{
		if ([event keyCode] == 53)
		{
			return VK_ESCAPE;
		}

		NSString* characters = [event charactersIgnoringModifiers];
		if ([characters length] == 0)
		{
			return -1;
		}

		unichar ch = [characters characterAtIndex:0];
		if (ch >= 'a' && ch <= 'z')
		{
			ch = ch - 'a' + 'A';
		}
		return ch < 256 ? static_cast<int>(ch) : -1;
	}

	MouseButton buttonFromEvent(NSEvent* event)
	{
		if ([event type] == NSEventTypeRightMouseDown || [event type] == NSEventTypeRightMouseUp)
		{
			return MouseRight;
		}
		if ([event type] == NSEventTypeOtherMouseDown || [event type] == NSEventTypeOtherMouseUp)
		{
			return MouseMiddle;
		}
		return MouseLeft;
	}
}

@interface GEBMetalView : MTKView
@property(nonatomic, assign) Window* owner;
@end

@implementation GEBMetalView
- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (void)keyDown:(NSEvent*)event
{
	const int key = keyFromEvent(event);
	if (key >= 0)
	{
		self.owner->setKeyState(key, true);
	}
}

- (void)keyUp:(NSEvent*)event
{
	const int key = keyFromEvent(event);
	if (key >= 0)
	{
		self.owner->setKeyState(key, false);
	}
}

- (void)updateMouse:(NSEvent*)event
{
	NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
	const int x = static_cast<int>(point.x);
	const int y = static_cast<int>(self.bounds.size.height - point.y);
	self.owner->setMousePosition(x, y);
}

- (void)mouseMoved:(NSEvent*)event
{
	[self updateMouse:event];
}

- (void)mouseDragged:(NSEvent*)event
{
	[self updateMouse:event];
}

- (void)rightMouseDragged:(NSEvent*)event
{
	[self updateMouse:event];
}

- (void)otherMouseDragged:(NSEvent*)event
{
	[self updateMouse:event];
}

- (void)mouseDown:(NSEvent*)event
{
	[self updateMouse:event];
	self.owner->setMouseButtonState(buttonFromEvent(event), MouseDown);
}

- (void)rightMouseDown:(NSEvent*)event
{
	[self updateMouse:event];
	self.owner->setMouseButtonState(buttonFromEvent(event), MouseDown);
}

- (void)otherMouseDown:(NSEvent*)event
{
	[self updateMouse:event];
	self.owner->setMouseButtonState(buttonFromEvent(event), MouseDown);
}

- (void)mouseUp:(NSEvent*)event
{
	[self updateMouse:event];
	self.owner->setMouseButtonState(buttonFromEvent(event), MouseUp);
}

- (void)rightMouseUp:(NSEvent*)event
{
	[self updateMouse:event];
	self.owner->setMouseButtonState(buttonFromEvent(event), MouseUp);
}

- (void)otherMouseUp:(NSEvent*)event
{
	[self updateMouse:event];
	self.owner->setMouseButtonState(buttonFromEvent(event), MouseUp);
}

- (void)scrollWheel:(NSEvent*)event
{
	[self updateMouse:event];
	self.owner->addMouseWheelDelta(static_cast<int>([event scrollingDeltaY]));
}
@end

@interface GEBWindowDelegate : NSObject<NSWindowDelegate>
@end

@implementation GEBWindowDelegate
- (void)windowWillClose:(NSNotification*)notification
{
	std::exit(0);
}
@end

namespace GamesEngineeringBase
{
	void Window::create(unsigned int window_width, unsigned int window_height, const std::string window_name, bool window_fullscreen, int window_x, int window_y)
	{
		width = window_width;
		height = window_height;
		image = new unsigned char[width * height * 3];
		clear();

		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		MacWindowImpl* mac = new MacWindowImpl();
		impl = mac;
		mac->device = MTLCreateSystemDefaultDevice();
		mac->commandQueue = [mac->device newCommandQueue];
		mac->rgba.resize(width * height * 4);

		NSRect contentRect = NSMakeRect(window_x, window_y, width, height);
		NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
		mac->window = [[NSWindow alloc] initWithContentRect:contentRect styleMask:style backing:NSBackingStoreBuffered defer:NO];
		[mac->window setTitle:[NSString stringWithUTF8String:window_name.c_str()]];
		[mac->window setDelegate:[GEBWindowDelegate new]];

		GEBMetalView* view = [[GEBMetalView alloc] initWithFrame:contentRect device:mac->device];
		view.owner = this;
		view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
		view.framebufferOnly = YES;
		view.paused = YES;
		view.enableSetNeedsDisplay = NO;
		view.drawableSize = CGSizeMake(width, height);
		view.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
		mac->view = view;
		[mac->window setContentView:view];
		[mac->window makeFirstResponder:view];

		if (window_fullscreen)
		{
			[mac->window toggleFullScreen:nil];
		}

		NSError* error = nil;
		NSString* shaderSource =
			@"#include <metal_stdlib>\n"
			"using namespace metal;\n"
			"struct VSOut { float4 position [[position]]; float2 uv; };\n"
			"vertex VSOut vertexMain(uint vertexID [[vertex_id]]) {\n"
			"    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };\n"
			"    float2 uvs[3] = { float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0) };\n"
			"    VSOut out;\n"
			"    out.position = float4(positions[vertexID], 0.0, 1.0);\n"
			"    out.uv = uvs[vertexID];\n"
			"    return out;\n"
			"}\n"
			"fragment float4 fragmentMain(VSOut in [[stage_in]], texture2d<float> image [[texture(0)]]) {\n"
			"    constexpr sampler s(address::clamp_to_edge, filter::nearest);\n"
			"    return image.sample(s, in.uv);\n"
			"}\n";
		id<MTLLibrary> library = [mac->device newLibraryWithSource:shaderSource options:nil error:&error];
		if (!library)
		{
			NSLog(@"Metal shader compile error: %@", error);
			std::exit(1);
		}

		MTLRenderPipelineDescriptor* pipelineDescriptor = [MTLRenderPipelineDescriptor new];
		pipelineDescriptor.vertexFunction = [library newFunctionWithName:@"vertexMain"];
		pipelineDescriptor.fragmentFunction = [library newFunctionWithName:@"fragmentMain"];
		pipelineDescriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
		mac->pipeline = [mac->device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
		if (!mac->pipeline)
		{
			NSLog(@"Metal pipeline error: %@", error);
			std::exit(1);
		}

		MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width height:height mipmapped:NO];
		textureDescriptor.usage = MTLTextureUsageShaderRead;
		mac->texture = [mac->device newTextureWithDescriptor:textureDescriptor];

		[mac->window center];
		[mac->window makeKeyAndOrderFront:nil];
		[NSApp activateIgnoringOtherApps:YES];
	}

	void Window::checkInput()
	{
		for (int i = 0; i < 3; ++i)
		{
			if (buttonStates[i] == MouseDown)
			{
				buttonStates[i] = MousePressed;
			}
		}

		NSEvent* event = nil;
		while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES]))
		{
			[NSApp sendEvent:event];
		}
	}

	unsigned char* Window::backBuffer() const
	{
		return image;
	}

	void Window::draw(int x, int y, unsigned char r, unsigned char g, unsigned char b)
	{
		const int index = ((y * width) + x) * 3;
		image[index] = r;
		image[index + 1] = g;
		image[index + 2] = b;
	}

	void Window::draw(int pixelIndex, unsigned char r, unsigned char g, unsigned char b)
	{
		const int index = pixelIndex * 3;
		image[index] = r;
		image[index + 1] = g;
		image[index + 2] = b;
	}

	void Window::draw(int x, int y, unsigned char* pixel)
	{
		draw(x, y, pixel[0], pixel[1], pixel[2]);
	}

	void Window::clear()
	{
		std::memset(image, 0, width * height * 3);
	}

	void Window::present()
	{
		MacWindowImpl* mac = static_cast<MacWindowImpl*>(impl);
		for (unsigned int i = 0; i < width * height; ++i)
		{
			mac->rgba[i * 4] = image[i * 3];
			mac->rgba[i * 4 + 1] = image[i * 3 + 1];
			mac->rgba[i * 4 + 2] = image[i * 3 + 2];
			mac->rgba[i * 4 + 3] = 255;
		}

		MTLRegion region = MTLRegionMake2D(0, 0, width, height);
		[mac->texture replaceRegion:region mipmapLevel:0 withBytes:mac->rgba.data() bytesPerRow:width * 4];

		// This application drives rendering manually rather than through MTKView's
		// draw callback. currentDrawable is cached for an MTKView draw cycle, so
		// using it here can return a drawable that our previous frame presented.
		// Acquire directly from the layer to guarantee one fresh drawable per frame.
		CAMetalLayer* metalLayer = (CAMetalLayer*)mac->view.layer;
		id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
		if (!drawable)
		{
			checkInput();
			return;
		}

		MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
		passDescriptor.colorAttachments[0].texture = drawable.texture;
		passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
		passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
		passDescriptor.colorAttachments[0].clearColor = mac->view.clearColor;

		id<MTLCommandBuffer> commandBuffer = [mac->commandQueue commandBuffer];
		id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
		[encoder setRenderPipelineState:mac->pipeline];
		[encoder setFragmentTexture:mac->texture atIndex:0];
		[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		[encoder endEncoding];
		[commandBuffer presentDrawable:drawable];
		[commandBuffer commit];
		checkInput();
	}

	unsigned int Window::getWidth() const
	{
		return width;
	}

	unsigned int Window::getHeight() const
	{
		return height;
	}

	unsigned char* Window::getBackBuffer() const
	{
		return image;
	}

	bool Window::keyPressed(int key) const
	{
		return key >= 0 && key < 256 && keys[key];
	}

	bool Window::mouseButtonPressed(MouseButton button) const
	{
		return buttonStates[button] == MouseDown || buttonStates[button] == MousePressed;
	}

	MouseButtonState Window::mouseButtonState(MouseButton button) const
	{
		return buttonStates[button];
	}

	int Window::getMouseX() const
	{
		return mousex;
	}

	int Window::getMouseY() const
	{
		return mousey;
	}

	int Window::getMouseWheel() const
	{
		return mouseWheel;
	}

	void Window::resetMouseWheelPosition()
	{
		mouseWheel = 0;
	}

	int Window::getMouseInWindowX() const
	{
		return mousex;
	}

	int Window::getMouseInWindowY() const
	{
		return mousey;
	}

	void Window::clipMouseToWindow() const
	{
	}

	Window::~Window()
	{
		delete[] image;
		delete static_cast<MacWindowImpl*>(impl);
	}

	void Window::setKeyState(int key, bool pressed)
	{
		if (key >= 0 && key < 256)
		{
			keys[key] = pressed;
		}
	}

	void Window::setMousePosition(int x, int y)
	{
		mousex = x;
		mousey = y;
	}

	void Window::setMouseButtonState(MouseButton button, MouseButtonState state)
	{
		buttonStates[button] = state;
	}

	void Window::addMouseWheelDelta(int delta)
	{
		mouseWheel += delta;
	}

	Image::Image(Image&& other)
	{
		width = other.width;
		height = other.height;
		channels = other.channels;
		data = other.data;
		other.width = 0;
		other.height = 0;
		other.channels = 0;
		other.data = nullptr;
	}

	Image& Image::operator=(Image&& other)
	{
		if (this != &other)
		{
			free();
			width = other.width;
			height = other.height;
			channels = other.channels;
			data = other.data;
			other.width = 0;
			other.height = 0;
			other.channels = 0;
			other.data = nullptr;
		}
		return *this;
	}

	bool Image::load(std::string filename)
	{
		free();
		CFStringRef path = CFStringCreateWithCString(nullptr, filename.c_str(), kCFStringEncodingUTF8);
		CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, false);
		CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
		CFRelease(url);
		CFRelease(path);
		if (!source)
		{
			return false;
		}

		CGImageRef imageRef = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
		CFRelease(source);
		if (!imageRef)
		{
			return false;
		}

		width = static_cast<unsigned int>(CGImageGetWidth(imageRef));
		height = static_cast<unsigned int>(CGImageGetHeight(imageRef));
		channels = 4;
		data = new unsigned char[width * height * channels];
		CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
		CGContextRef context = CGBitmapContextCreate(data, width, height, 8, width * channels, colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
		CGContextDrawImage(context, CGRectMake(0, 0, width, height), imageRef);
		CGContextRelease(context);
		CGColorSpaceRelease(colorSpace);
		CGImageRelease(imageRef);
		return true;
	}

	unsigned char* Image::at(const unsigned int x, const unsigned int y) const
	{
		return &data[((std::min(y, height - 1) * width) + std::min(x, width - 1)) * channels];
	}

	unsigned char Image::alphaAt(const unsigned int x, const unsigned int y) const
	{
		if (channels == 4)
		{
			return data[((std::min(y, height - 1) * width) + std::min(x, width - 1)) * channels + 3];
		}
		return 255;
	}

	unsigned char Image::at(const unsigned int x, const unsigned int y, const unsigned int index) const
	{
		return data[(((std::min(y, height - 1) * width) + std::min(x, width - 1)) * channels) + index];
	}

	unsigned char* Image::atUnchecked(const unsigned int x, const unsigned int y) const
	{
		return &data[((y * width) + x) * channels];
	}

	unsigned char Image::alphaAtUnchecked(const unsigned int x, const unsigned int y) const
	{
		if (channels == 4)
		{
			return data[(((y * width) + x) * channels) + 3];
		}
		return 255;
	}

	bool Image::hasAlpha() const
	{
		return channels == 4;
	}

	void Image::free()
	{
		delete[] data;
		data = nullptr;
		width = 0;
		height = 0;
		channels = 0;
	}

	Image::~Image()
	{
		free();
	}
}

#endif

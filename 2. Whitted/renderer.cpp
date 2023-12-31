#include "precomp.h"

// -----------------------------------------------------------
// Initialize the renderer
// -----------------------------------------------------------
void Renderer::Init()
{
	// create fp32 rgb pixel buffer to render to
	accumulator = (float4*)MALLOC64( SCRWIDTH * SCRHEIGHT * 16 );
	memset( accumulator, 0, SCRWIDTH * SCRHEIGHT * 16 );
	// retrieve cam
	FILE* f = fopen( "appstate.dat", "rb" );
	if (f)
	{
		fread( &camera, 1, sizeof( Camera ), f );
		fclose( f );
	}
}

// -----------------------------------------------------------
// Gather direct illumination for a point
// -----------------------------------------------------------
float3 Renderer::DirectIllumination( const float3& I, const float3& N )
{
	// sum irradiance from light sources
	float3 irradiance( 0 );
	// query the (only) scene light
	float3 pointOnLight = scene.GetLightPos();
	float3 L = pointOnLight - I;
	float distance = length( L );
	L *= 1 / distance;
	float ndotl = dot( N, L );
	if (ndotl < EPSILON) /* we don't face the light */ return 0;
	// cast a shadow ray
	Ray s( I + L * EPSILON, L, distance - 2 * EPSILON );
	if (!scene.IsOccluded( s ))
	{
		// light is visible; calculate irradiance (= projected radiance)
		float attenuation = 1 / (distance * distance);
		float3 in_radiance = scene.GetLightColor() * attenuation;
		irradiance = in_radiance * dot( N, L );
	}
	return irradiance;
}

// -----------------------------------------------------------
// Evaluate light transport
// -----------------------------------------------------------
float3 Renderer::Trace( Ray& ray, int depth )
{
	// intersect the ray with the scene
	scene.FindNearest( ray );
	if (ray.objIdx == -1) /* ray left the scene */ return 0;
	if (depth > MAXDEPTH) /* bouned too many times */ return 0;
	// gather shading data
	float3 I = ray.O + ray.t * ray.D;
	float3 N = scene.GetNormal( ray.objIdx, I, ray.D );
	float3 albedo = scene.GetAlbedo( ray.objIdx, I );
	// do whitted
	float3 out_radiance( 0 );
	float reflectivity = scene.GetReflectivity( ray.objIdx, I );
	float refractivity = scene.GetRefractivity( ray.objIdx, I );
	float diffuseness = 1 - (reflectivity + refractivity);
	// handle pure speculars such as mirrors
	if (reflectivity > 0)
	{
		float3 R = reflect( ray.D, N );
		Ray r( I + R * EPSILON, R );
		out_radiance += reflectivity * albedo * Trace( r, depth + 1 );
	}
	// handle dielectrics such as glass / water
	if (refractivity > 0)
	{
		float3 R = reflect( ray.D, N );
		Ray r( I + R * EPSILON, R );
		float n1 = ray.inside ? 1.2f : 1, n2 = ray.inside ? 1 : 1.2f;
		float eta = n1 / n2, cosi = dot( -ray.D, N );
		float cost2 = 1.0f - eta * eta * (1 - cosi * cosi);
		float Fr = 1;
		if (cost2 > 0)
		{
			float a = n1 - n2, b = n1 + n2, R0 = (a * a) / (b * b), c = 1 - cosi;
			Fr = R0 + (1 - R0) * (c * c * c * c * c);
			float3 T = eta * ray.D + ((eta * cosi - sqrtf( fabs( cost2 ) )) * N);
			Ray t( I + T * EPSILON, T );
			t.inside = !ray.inside;
			out_radiance += albedo * (1 - Fr) * Trace( t, depth + 1 );
		}
		out_radiance += albedo * Fr * Trace( r, depth + 1 );
	}
	// handle diffuse surfaces
	if (diffuseness > 0)
	{
		// calculate illumination
		float3 irradiance = DirectIllumination( I, N );
		// we don't account for diffuse interreflections: approximate
		float3 ambient = float3( 0.2f, 0.2f, 0.2f );
		// calculate reflected radiance using Lambert brdf
		float3 brdf = albedo * INVPI;
		out_radiance += diffuseness * brdf * (irradiance + ambient);
	}
	// apply absorption if we travelled through a medium
	float3 medium_scale( 1 );
	if (ray.inside)
	{
		float3 absorption = float3( 0.5f, 0, 0.5f ); // scene.GetAbsorption( objIdx );
		medium_scale.x = expf( absorption.x * -ray.t );
		medium_scale.y = expf( absorption.y * -ray.t );
		medium_scale.z = expf( absorption.z * -ray.t );
	}
	return medium_scale * out_radiance;
}

// -----------------------------------------------------------
// Main application tick function - Executed once per frame
// -----------------------------------------------------------
void Renderer::Tick( float deltaTime )
{
	// animation
	if (animating) scene.SetTime( anim_time += deltaTime * 0.002f );
	// pixel loop
	Timer t;
	// lines are executed as OpenMP parallel tasks (disabled in DEBUG)
#pragma omp parallel for schedule(dynamic)
	for (int y = 0; y < SCRHEIGHT; y++)
	{
		// trace a primary ray for each pixel on the line
		for (int x = 0; x < SCRWIDTH; x++)
			accumulator[x + y * SCRWIDTH] =
			float4( Trace( camera.GetPrimaryRay( (float)x, (float)y ) ), 0 );
		// translate accumulator contents to rgb32 pixels
		for (int dest = y * SCRWIDTH, x = 0; x < SCRWIDTH; x++)
			screen->pixels[dest + x] =
			RGBF32_to_RGB8( &accumulator[x + y * SCRWIDTH] );	
	}
	// performance report - running average - ms, MRays/s
	avg = (1 - alpha) * avg + alpha * t.elapsed() * 1000;
	float fps = 1000.0f / avg, rps = ( SCRWIDTH * SCRHEIGHT ) / avg;
	printf( "%5.2fms (%.1ffps) - %.1fMrays/s\n", avg, fps, rps / 1000 );
	if (alpha > 0.05f) alpha *= 0.75f;
	// handle user input
	camera.HandleInput( deltaTime );
}

// -----------------------------------------------------------
// Update user interface (imgui)
// -----------------------------------------------------------
void Renderer::UI()
{
	// animation toggle
	ImGui::Checkbox( "Animate scene", &animating );
	// ray query on mouse
	Ray r = camera.GetPrimaryRay( (float)mousePos.x, (float)mousePos.y );
	scene.FindNearest( r );
	ImGui::Text( "Object id %i", r.objIdx );
	ImGui::Text( "Frame: %5.2fms (%.1ffps)", avg, 1000 / avg );
}
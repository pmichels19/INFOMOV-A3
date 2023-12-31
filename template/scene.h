#pragma once

// -----------------------------------------------------------
// scene.h
// Simple test scene for ray tracing experiments. Goals:
// - Super-fast scene intersection
// - Easy interface: scene.FindNearest / IsOccluded
// - With normals and albedo: GetNormal / GetAlbedo
// - Area light source (animated), for light transport
// - Primitives can be hit from inside - for dielectrics
// - Can be extended with other primitives and/or a BVH
// - Optionally animated - for temporal experiments
// - Not everything is axis aligned - for cache experiments
// - Can be evaluated at arbitrary time - for motion blur
// - Has some high-frequency details - for filtering
// Some speed tricks that severely affect maintainability
// are enclosed in #ifdef SPEEDTRIX / #endif. Mind these
// if you plan to alter the scene in any way.
// -----------------------------------------------------------

// INFOMOV'23: don't disable these
#define SPEEDTRIX
#define FOURLIGHTS
#define USEBVH

#define PLANE_X(o,i) {t=-(ray.O.x+o)*ray.rD.x;if(t<ray.t&&t>0)ray.t=t,ray.objIdx=i;}
#define PLANE_Y(o,i) {t=-(ray.O.y+o)*ray.rD.y;if(t<ray.t&&t>0)ray.t=t,ray.objIdx=i;}
#define PLANE_Z(o,i) {t=-(ray.O.z+o)*ray.rD.z;if(t<ray.t&&t>0)ray.t=t,ray.objIdx=i;}

namespace Tmpl8 {

    __declspec( align( 64 ) ) class Ray {
    public:
        Ray() = default;
        Ray( const float3 origin, const float3 direction, const float distance = 1e34f, const int idx = -1 ) {
            O = origin, D = direction, t = distance;
            // calculate reciprocal ray direction for triangles and AABBs
            rD = float3( 1 / D.x, 1 / D.y, 1 / D.z );
#ifdef SPEEDTRIX
            d0 = 1, d1 = d2 = 0; // ready for SIMD matrix math
#endif
            objIdx = idx;
        }
        float3 IntersectionPoint() const { return O + t * D; }
        // ray data
#ifndef SPEEDTRIX
        float3 O, D, rD;
#else
        union { struct { float3 O; float d0; }; __m128 O4; };
        union { struct { float3 D; float d1; }; __m128 D4; };
        union { struct { float3 rD; float d2; }; __m128 rD4; };
#endif
        float t = 1e34f;
        int objIdx = -1;
        bool inside = false; // true when in medium
    };

    // -----------------------------------------------------------
    // Sphere primitive
    // Basic sphere, with explicit support for rays that start
    // inside it. Good candidate for a dielectric material.
    // -----------------------------------------------------------
    class Sphere {
    public:
        Sphere() = default;
        Sphere( int idx, float3 p, float r ):
            pos( p ), r2( r* r ), invr( 1 / r ), objIdx( idx ) {
        }

        void Intersect( Ray& ray ) const {
            float3 oc = ray.O - this->pos;
            float b = dot( oc, ray.D );
            float c = dot( oc, oc ) - this->r2;
            float t, d = b * b - c;
            if ( d <= 0 ) return;

            d = sqrtf( d );
            t = -b - d;
            bool hit = t < ray.t && t > 0;
            if ( hit ) {
                ray.t = t, ray.objIdx = objIdx;
                return;
            }

            if ( c > 0 ) return; // we're outside; safe to skip option 2

            t = d - b;
            hit = t < ray.t && t > 0;
            if ( hit ) {
                ray.t = t;
                ray.objIdx = objIdx;
            }
        }

        bool IsOccluded( const Ray& ray ) const {
            float3 oc = ray.O - this->pos;
            float b = dot( oc, ray.D );
            float c = dot( oc, oc ) - this->r2;
            float t, d = b * b - c;
            if ( d <= 0 ) return false;

            d = sqrtf( d );
            t = -b - d;
            bool hit = t < ray.t && t > 0;
            return hit;
        }

        float3 GetNormal( const float3 I ) const {
            return ( I - this->pos ) * invr;
        }

        float3 GetAlbedo( const float3 I ) const {
            return float3( 0.93f );
        }

        float3 pos = 0;
        float r2 = 0, invr = 0;
        int objIdx = -1;
    };

    // -----------------------------------------------------------
    // Plane primitive
    // Basic infinite plane, defined by a normal and a distance
    // from the origin (in the direction of the normal).
    // -----------------------------------------------------------
    class Plane {
    public:
        Plane() = default;
        Plane( int idx, float3 normal, float dist ): N( normal ), d( dist ), objIdx( idx ) {}
        void Intersect( Ray& ray ) const {
            float t = -( dot( ray.O, this->N ) + this->d ) / ( dot( ray.D, this->N ) );
            if ( t < ray.t && t > 0 ) {
                ray.t = t;
                ray.objIdx = objIdx;
            }
        }

        float3 GetNormal( const float3 I ) const {
            return N;
        }

        float3 GetAlbedo( const float3 I ) const {
            if ( N.y == 1 ) {
                // floor albedo: checkerboard
                int ix = (int) ( I.x * 2 + 96.01f );
                int iz = (int) ( I.z * 2 + 96.01f );
                // add deliberate aliasing to two tile
                if ( ix == 98 && iz == 98 ) ix = (int) ( I.x * 32.01f ), iz = (int) ( I.z * 32.01f );
                if ( ix == 94 && iz == 98 ) ix = (int) ( I.x * 64.01f ), iz = (int) ( I.z * 64.01f );
                return float3( ( ( ix + iz ) & 1 ) ? 1 : 0.3f );
            } else if ( N.z == -1 ) {
                // back wall: logo
                static Surface logo( "../assets/logo.png" );
                int ix = (int) ( ( I.x + 4 ) * ( 128.0f / 8 ) ), iy = (int) ( ( 2 - I.y ) * ( 64.0f / 3 ) );
                uint p = logo.pixels[( ix & 127 ) + ( iy & 63 ) * 128];
                uint3 i3( ( p >> 16 ) & 255, ( p >> 8 ) & 255, p & 255 );
                return float3( i3 ) * ( 1.0f / 255.0f );
            } else if ( N.x == 1 ) {
                // left wall: red
                static Surface red( "../assets/red.png" );
                int ix = (int) ( ( I.z - 4 ) * ( 512.0f / 7 ) ), iy = (int) ( ( 2 - I.y ) * ( 512.0f / 3 ) );
                uint p = red.pixels[( ix & 511 ) + ( iy & 511 ) * 512];
                uint3 i3( ( p >> 16 ) & 255, ( p >> 8 ) & 255, p & 255 );
                return float3( i3 ) * ( 1.0f / 255.0f );
            } else if ( N.x == -1 ) {
                // right wall: blue
                static Surface blue( "../assets/blue.png" );
                int ix = (int) ( ( I.z - 4 ) * ( 512.0f / 7 ) ), iy = (int) ( ( 2 - I.y ) * ( 512.0f / 3 ) );
                uint p = blue.pixels[( ix & 511 ) + ( iy & 511 ) * 512];
                uint3 i3( ( p >> 16 ) & 255, ( p >> 8 ) & 255, p & 255 );
                return float3( i3 ) * ( 1.0f / 255.0f );
            }
            return float3( 0.93f );
        }

        float3 N;
        float d;
        int objIdx = -1;
    };

    // -----------------------------------------------------------
    // Cube primitive
    // Oriented cube. Unsure if this will also work for rays that
    // start inside it; maybe not the best candidate for testing
    // dielectrics.
    // -----------------------------------------------------------
    class Cube {
    public:
        Cube() = default;
        Cube( int idx, float3 pos, float3 size, mat4 transform = mat4::Identity() ) {
            objIdx = idx;
            b[0] = float4( pos - 0.5f * size, 1 );
            b[1] = float4( pos + 0.5f * size, 1 );
            M = transform;
            invM = transform.FastInvertedTransformNoScale();
        }
        void Intersect( Ray& ray ) const {
            // 'rotate' the cube by transforming the ray into object space
            // using the inverse of the cube transform.
#ifdef SPEEDTRIX
            // an AABB can be efficiently tested with SIMD - and matrix math is easy too. Profit!
            const __m128 a4 = ray.O4;
            const __m128 b4 = ray.D4;
            __m128 v0 = _mm_mul_ps( a4, _mm_load_ps( &invM.cell[0] ) );
            __m128 v1 = _mm_mul_ps( a4, _mm_load_ps( &invM.cell[4] ) );
            __m128 v2 = _mm_mul_ps( a4, _mm_load_ps( &invM.cell[8] ) );
            __m128 v3 = _mm_mul_ps( a4, _mm_load_ps( &invM.cell[12] ) );
            _MM_TRANSPOSE4_PS( v0, v1, v2, v3 );
            __m128 o4 = _mm_add_ps( _mm_add_ps( v0, v1 ), _mm_add_ps( v2, v3 ) );
            __m128 v4 = _mm_mul_ps( b4, _mm_load_ps( &invM.cell[0] ) );
            __m128 v5 = _mm_mul_ps( b4, _mm_load_ps( &invM.cell[4] ) );
            __m128 v6 = _mm_mul_ps( b4, _mm_load_ps( &invM.cell[8] ) );
            __m128 v7 = _mm_mul_ps( b4, _mm_load_ps( &invM.cell[12] ) );
            _MM_TRANSPOSE4_PS( v4, v5, v6, v7 );
            __m128 d4 = _mm_add_ps( _mm_add_ps( v4, v5 ), v6 );
            __m128 rd4 = _mm_div_ps( _mm_set1_ps( 1.0f ), d4 );
            // AABB test
            __m128 t1 = _mm_mul_ps( _mm_sub_ps( bmin4, o4 ), rd4 );
            __m128 t2 = _mm_mul_ps( _mm_sub_ps( bmax4, o4 ), rd4 );
            __m128 vmax4 = _mm_max_ps( t1, t2 );
            __m128 vmin4 = _mm_min_ps( t1, t2 );
            float tmax = min( vmax4.m128_f32[0], min( vmax4.m128_f32[1], vmax4.m128_f32[2] ) );
            float tmin = max( vmin4.m128_f32[0], max( vmin4.m128_f32[1], vmin4.m128_f32[2] ) );
            if ( tmin < tmax ) if ( tmin > 0 ) {
                if ( tmin < ray.t ) {
                    ray.t = tmin;
                    ray.objIdx = objIdx;
                }
            } else if ( tmax > 0 ) {
                if ( tmax < ray.t ) {
                    ray.t = tmax;
                    ray.objIdx = objIdx;
                }
            }
#else
            float3 O = TransformPosition( ray.O, invM );
            float3 D = TransformVector( ray.D, invM );
            float rDx = 1 / D.x, rDy = 1 / D.y, rDz = 1 / D.z;
            int signx = D.x < 0, signy = D.y < 0, signz = D.z < 0;
            float tmin = ( b[signx].x - O.x ) * rDx;
            float tmax = ( b[1 - signx].x - O.x ) * rDx;
            float tymin = ( b[signy].y - O.y ) * rDy;
            float tymax = ( b[1 - signy].y - O.y ) * rDy;
            if ( tmin > tymax || tymin > tmax ) return;
            tmin = max( tmin, tymin ), tmax = min( tmax, tymax );
            float tzmin = ( b[signz].z - O.z ) * rDz;
            float tzmax = ( b[1 - signz].z - O.z ) * rDz;
            if ( tmin > tzmax || tzmin > tmax ) return;
            tmin = max( tmin, tzmin ), tmax = min( tmax, tzmax );
            if ( tmin > 0 ) {
                if ( tmin < ray.t ) ray.t = tmin, ray.objIdx = objIdx;
            } else if ( tmax > 0 ) {
                if ( tmax < ray.t ) ray.t = tmax, ray.objIdx = objIdx;
            }
#endif
        }

        bool IsOccluded( const Ray& ray ) const {
#ifdef SPEEDTRIX
            // an AABB can be efficiently tested with SIMD - and matrix math is easy too. Profit!
            const __m128 a4 = ray.O4;
            const __m128 b4 = ray.D4;
            __m128 v0 = _mm_mul_ps( a4, _mm_load_ps( &invM.cell[0] ) );
            __m128 v1 = _mm_mul_ps( a4, _mm_load_ps( &invM.cell[4] ) );
            __m128 v2 = _mm_mul_ps( a4, _mm_load_ps( &invM.cell[8] ) );
            __m128 v3 = _mm_mul_ps( a4, _mm_load_ps( &invM.cell[12] ) );
            _MM_TRANSPOSE4_PS( v0, v1, v2, v3 );
            __m128 o4 = _mm_add_ps( _mm_add_ps( v0, v1 ), _mm_add_ps( v2, v3 ) );
            __m128 v4 = _mm_mul_ps( b4, _mm_load_ps( &invM.cell[0] ) );
            __m128 v5 = _mm_mul_ps( b4, _mm_load_ps( &invM.cell[4] ) );
            __m128 v6 = _mm_mul_ps( b4, _mm_load_ps( &invM.cell[8] ) );
            __m128 v7 = _mm_mul_ps( b4, _mm_load_ps( &invM.cell[12] ) );
            _MM_TRANSPOSE4_PS( v4, v5, v6, v7 );
            __m128 d4 = _mm_add_ps( _mm_add_ps( v4, v5 ), v6 );
            //__m128 rd4 = _mm_div_ps( _mm_set1_ps( 1.0f ), d4 );
            __m128 rd4 = _mm_rcp_ps( d4 ); // reduced precision unsufficient?
            // AABB test
            __m128 t1 = _mm_mul_ps( _mm_sub_ps( bmin4, o4 ), rd4 );
            __m128 t2 = _mm_mul_ps( _mm_sub_ps( bmax4, o4 ), rd4 );
            __m128 vmax4 = _mm_max_ps( t1, t2 ), vmin4 = _mm_min_ps( t1, t2 );
            float tmax = min( vmax4.m128_f32[0], min( vmax4.m128_f32[1], vmax4.m128_f32[2] ) );
            float tmin = max( vmin4.m128_f32[0], max( vmin4.m128_f32[1], vmin4.m128_f32[2] ) );
            return tmax > 0 && tmin < tmax && tmin < ray.t;
#else
            float3 O = TransformPosition_SSE( ray.O4, invM );
            float3 D = TransformVector_SSE( ray.D4, invM );
            float rDx = 1 / D.x, rDy = 1 / D.y, rDz = 1 / D.z;
            float t1 = ( b[0].x - O.x ) * rDx, t2 = ( b[1].x - O.x ) * rDx;
            float t3 = ( b[0].y - O.y ) * rDy, t4 = ( b[1].y - O.y ) * rDy;
            float t5 = ( b[0].z - O.z ) * rDz, t6 = ( b[1].z - O.z ) * rDz;
            float tmin = max( max( min( t1, t2 ), min( t3, t4 ) ), min( t5, t6 ) );
            float tmax = min( min( max( t1, t2 ), max( t3, t4 ) ), max( t5, t6 ) );
            return tmax > 0 && tmin < tmax && tmin < ray.t;
#endif
        }

        float3 GetNormal( const float3 I ) const {
            // transform intersection point to object space
            float3 objI = TransformPosition( I, invM );

            // determine normal in object space
            float3 N = float3( -1, 0, 0 );

            float d0 = fabs( objI.x - b[0].x );
            float d1 = fabs( objI.x - b[1].x );
            float d2 = fabs( objI.y - b[0].y );
            float d3 = fabs( objI.y - b[1].y );
            float d4 = fabs( objI.z - b[0].z );
            float d5 = fabs( objI.z - b[1].z );

            float minDist = d0;
            if ( d1 < minDist ) minDist = d1, N.x = 1;
            if ( d2 < minDist ) minDist = d2, N = float3( 0, -1, 0 );
            if ( d3 < minDist ) minDist = d3, N = float3( 0, 1, 0 );
            if ( d4 < minDist ) minDist = d4, N = float3( 0, 0, -1 );
            if ( d5 < minDist ) minDist = d5, N = float3( 0, 0, 1 );
            // return normal in world space
            return TransformVector( N, M );
        }

        float3 GetAlbedo( const float3 I ) const {
            return float3( 1, 1, 1 );
        }

#ifdef SPEEDTRIX
        union { float4 b[2]; struct { __m128 bmin4, bmax4; }; };
#else
        float3 b[2];
#endif
        mat4 M;
        mat4 invM;
        int objIdx = -1;
    };

    // -----------------------------------------------------------
    // Quad primitive
    // Oriented quad, intended to be used as a light source.
    // -----------------------------------------------------------
    class Quad {
    public:
        Quad() = default;
        Quad( int idx, float s, mat4 transform = mat4::Identity() ) {
            objIdx = idx;
            size = s * 0.5f;
            T = transform;
            invT = transform.FastInvertedTransformNoScale();
        }

        void Intersect( Ray& ray ) const {
            const float Oy = invT.cell[4] * ray.O.x + invT.cell[5] * ray.O.y + invT.cell[6] * ray.O.z + invT.cell[7];
            const float Dy = invT.cell[4] * ray.D.x + invT.cell[5] * ray.D.y + invT.cell[6] * ray.D.z;
            const float t = Oy / -Dy;
            if ( t < ray.t && t > 0 ) {
                const float Ox = invT.cell[0] * ray.O.x + invT.cell[1] * ray.O.y + invT.cell[2] * ray.O.z + invT.cell[3];
                const float Oz = invT.cell[8] * ray.O.x + invT.cell[9] * ray.O.y + invT.cell[10] * ray.O.z + invT.cell[11];
                const float Dx = invT.cell[0] * ray.D.x + invT.cell[1] * ray.D.y + invT.cell[2] * ray.D.z;
                const float Dz = invT.cell[8] * ray.D.x + invT.cell[9] * ray.D.y + invT.cell[10] * ray.D.z;
                const float Ix = Ox + t * Dx;
                const float Iz = Oz + t * Dz;
                if ( Ix > -size && Ix < size && Iz > -size && Iz < size ) {
                    ray.t = t;
                    ray.objIdx = objIdx;
                }
            }
        }

        bool IsOccluded( const Ray& ray ) const {
            const float Oy = invT.cell[4] * ray.O.x + invT.cell[5] * ray.O.y + invT.cell[6] * ray.O.z + invT.cell[7];
            const float Dy = invT.cell[4] * ray.D.x + invT.cell[5] * ray.D.y + invT.cell[6] * ray.D.z;
            const float t = Oy / -Dy;
            if ( t < ray.t && t > 0 ) {
                const float Ox = invT.cell[0] * ray.O.x + invT.cell[1] * ray.O.y + invT.cell[2] * ray.O.z + invT.cell[3];
                const float Oz = invT.cell[8] * ray.O.x + invT.cell[9] * ray.O.y + invT.cell[10] * ray.O.z + invT.cell[11];
                const float Dx = invT.cell[0] * ray.D.x + invT.cell[1] * ray.D.y + invT.cell[2] * ray.D.z;
                const float Dz = invT.cell[8] * ray.D.x + invT.cell[9] * ray.D.y + invT.cell[10] * ray.D.z;
                const float Ix = Ox + t * Dx;
                const float Iz = Oz + t * Dz;
                return Ix > -size && Ix < size && Iz > -size && Iz < size;
            }

            return false;
        }

        float3 GetNormal( const float3 I ) const {
            // TransformVector( float3( 0, -1, 0 ), T ) 
            return float3( -T.cell[1], -T.cell[5], -T.cell[9] );
        }

        float3 GetAlbedo( const float3 I ) const {
            return float3( 10 );
        }

        float size;
        mat4 T, invT;
        int objIdx = -1;
    };

    // -----------------------------------------------------------
    // Torus primitive - Inigo Quilez, ShaderToy 4sBGDy
    // -----------------------------------------------------------
    class Torus {
    public:
        Torus() = default;
        Torus::Torus( int idx, float a, float b ): objIdx( idx ) {
            rc2 = a * a, rt2 = b * b;
            T = invT = mat4::Identity();
            r2 = sqrf( a + b );
        }

        void Intersect( Ray& ray ) const {
            // via: https://www.shadertoy.com/view/4sBGDy
            float3 O = make_float3( ray.O.x + 0.25f, 0.707106829f * ray.O.y + 0.707106829f * ray.O.z - 1.41421366f, -0.707106829f * ray.O.y + 0.707106829f * ray.O.z - 1.41421366f );
            //float3 O = invT.TransformPoint( ray.O );
            float3 D = make_float3( ray.D.x, 0.707106829f * ray.D.y + 0.707106829f * ray.D.z, -0.707106829f * ray.D.y + 0.707106829f * ray.D.z );
            //float3 D = invT.TransformVector( ray.D );
            // extension rays need double precision for the quadratic solver!
            double po = 1;
            double m = dot( O, O );
            double k3 = dot( O, D );
            double k32 = k3 * k3;
            // bounding sphere test
            if ( k32 < m - 1.10249984 ) return;

            // setup torus intersection
            double k = ( m - 0.0625 - 0.640000045 ) * 0.5;
            double k2 = k32 + 0.640000045 * D.z * D.z + k;
            double k1 = k * k3 + 0.640000045 * O.z * D.z;
            double k0 = k * k + 0.640000045 * O.z * O.z - 0.640000045 * 0.0625;
            // solve quadratic equation
            if ( fabs( k3 * ( k32 - k2 ) + k1 ) < 0.0001 ) {
                swap( k1, k3 );
                po = -1;
                k0 = 1 / k0;
                k1 = k1 * k0;
                k2 = k2 * k0;
                k3 = k3 * k0;
                k32 = k3 * k3;
            }

            double c2 = 2 * k2 - 3 * k32;
            double c1 = k3 * ( k32 - k2 ) + k1;
            double c0 = k3 * ( k3 * ( -3 * k32 + 4 * k2 ) - 8 * k1 ) + 4 * k0;
            c2 *= 0.33333333333;
            c1 *= 2;
            c0 *= 0.33333333333;
            double Q = c2 * c2 + c0;
            double R = 3 * c0 * c2 - c2 * c2 * c2 - c1 * c1;
            double h = R * R - Q * Q * Q;
            double z;
            if ( h < 0 ) {
                const double sQ = sqrt( Q );
                z = 2 * sQ * cos( acos( R / ( sQ * Q ) ) * 0.33333333333 );
            } else {
                const double sQ = cbrt( sqrt( h ) + fabs( R ) ); // pow( sqrt( h ) + fabs( R ), 0.3333333 );
                z = copysign( fabs( sQ + Q / sQ ), R );
            }

            z = c2 - z;
            double d1 = z - 3 * c2;
            double d2 = z * z - 3 * c0;
            if ( fabs( d1 ) < 1.0e-8 ) {
                if ( d2 < 0 ) return;
                d2 = sqrt( d2 );
            } else {
                if ( d1 < 0 ) return;
                d1 = sqrt( d1 * 0.5 );
                d2 = c1 / d1;
            }

            double t = 1e20;
            h = d1 * d1 - z + d2;
            if ( h > 0 ) {
                h = sqrt( h );
                double t1 = -d1 - h - k3;
                double t2 = -d1 + h - k3;
                t1 = ( po < 0 ) ? 2 / t1 : t1;
                t2 = ( po < 0 ) ? 2 / t2 : t2;
                if ( t1 > 0 ) t = t1;
                if ( t2 > 0 ) t = min( t, t2 );
            }

            h = d1 * d1 - z - d2;
            if ( h > 0 ) {
                h = sqrt( h );
                double t1 = d1 - h - k3;
                double t2 = d1 + h - k3;
                t1 = ( po < 0 ) ? 2 / t1 : t1;
                t2 = ( po < 0 ) ? 2 / t2 : t2;
                if ( t1 > 0 ) t = min( t, t1 );
                if ( t2 > 0 ) t = min( t, t2 );
            }

            float ft = (float) t;
            if ( ft > 0 && ft < ray.t ) {
                ray.t = ft;
                ray.objIdx = objIdx;
            }
        }

        bool IsOccluded( const Ray& ray ) const {
            // via: https://www.shadertoy.com/view/4sBGDy
            float3 O = make_float3( ray.O.x + 0.25f, 0.707106829f * ray.O.y + 0.707106829f * ray.O.z - 1.41421366f, -0.707106829f * ray.O.y + 0.707106829f * ray.O.z - 1.41421366f );
            //float3 O = invT.TransformPoint( ray.O );
            float3 D = make_float3( ray.D.x, 0.707106829f * ray.D.y + 0.707106829f * ray.D.z, -0.707106829f * ray.D.y + 0.707106829f * ray.D.z );
            //float3 D = invT.TransformVector( ray.D );
            float po = 1;
            float m = dot( O, O );
            float k3 = dot( O, D );
            float k32 = k3 * k3;
            // bounding sphere test
            if ( k32 < m - 1.10249984 ) return false;

            // setup torus intersection
            float k = ( m - 0.0625 - 0.640000045 ) * 0.5f;
            float k2 = k32 + 0.640000045 * D.z * D.z + k;
            float k1 = k * k3 + 0.640000045 * O.z * D.z;
            float k0 = k * k + 0.640000045 * O.z * O.z - 0.640000045 * 0.0625;
            // solve quadratic equation
            if ( fabs( k3 * ( k32 - k2 ) + k1 ) < 0.01f ) {
                swap( k1, k3 );
                po = -1;
                k0 = 1 / k0;
                k1 = k1 * k0;
                k2 = k2 * k0;
                k3 = k3 * k0;
                k32 = k3 * k3;
            }

            float c2 = 2 * k2 - 3 * k32;
            float c1 = k3 * ( k32 - k2 ) + k1;
            float c0 = k3 * ( k3 * ( -3 * k32 + 4 * k2 ) - 8 * k1 ) + 4 * k0;
            c2 *= 0.33333333333f;
            c1 *= 2;
            c0 *= 0.33333333333f;

            float Q = c2 * c2 + c0;
            float R = 3 * c0 * c2 - c2 * c2 * c2 - c1 * c1;
            float h = R * R - Q * Q * Q;
            float z = 0;
            if ( h < 0 ) {
                const float sQ = sqrtf( Q );
                z = 2 * sQ * cosf( acosf( R / ( sQ * Q ) ) * 0.3333333f );
            } else {
                const float sQ = cbrtf( sqrtf( h ) + fabs( R ) ); // powf( sqrtf( h ) + fabs( R ), 0.3333333f );
                z = copysign( fabs( sQ + Q / sQ ), R );
            }

            z = c2 - z;
            float d1 = z - 3 * c2;
            float d2 = z * z - 3 * c0;
            if ( fabs( d1 ) < 1.0e-4f ) {
                if ( d2 < 0 ) return false;
                d2 = sqrtf( d2 );
            } else {
                if ( d1 < 0.0 ) return false;
                d1 = sqrtf( d1 * 0.5f );
                d2 = c1 / d1;
            }

            float t = 1e20f;
            h = d1 * d1 - z + d2;
            if ( h > 0 ) {
                float t1 = -d1 - sqrtf( h ) - k3;
                t1 = ( po < 0 ) ? 2 / t1 : t1;
                if ( t1 > 0 && t1 < ray.t ) return true;
            }

            h = d1 * d1 - z - d2;
            if ( h > 0 ) {
                float t1 = d1 - sqrtf( h ) - k3;
                t1 = ( po < 0 ) ? 2 / t1 : t1;
                if ( t1 > 0 && t1 < ray.t ) return true;
            }

            return false;
        }

        float3 GetNormal( const float3 I ) const {
            float3 L = TransformPosition( I, invT );
            float3 N = normalize( L * ( dot( L, L ) - 0.0625 - 0.640000045 * float3( 1, 1, -1 ) ) );
            return TransformVector( N, T );
        }

        float3 Torus::GetAlbedo( const float3 I ) const {
            return float3( 1 ); // material.albedo;
        }

        float rt2, rc2, r2;
        int objIdx;
        mat4 T, invT;
    };

    // -----------------------------------------------------------
    // Scene class
    // We intersect this. The query is internally forwarded to the
    // list of primitives, so that the nearest hit can be returned.
    // For this hit (distance, obj id), we can query the normal and
    // albedo.
    // -----------------------------------------------------------
    class Scene {
    public:
        Scene() {
            // we store all primitives in one continuous buffer
#ifdef FOURLIGHTS
            for ( int i = 0; i < 4; i++ ) quad[i] = Quad( 0, 0.5f );	// 0: four light sources
#else
            quad = Quad( 0, 1 );									// 0: light source
#endif
            sphere = Sphere( 1, float3( 0 ), 0.6f );				// 1: bouncing ball
            sphere2 = Sphere( 2, float3( 0, 2.5f, -3.07f ), 8 );	// 2: rounded corners
            cube = Cube( 3, float3( 0 ), float3( 1.15f ) );			// 3: cube
            plane[0] = Plane( 4, float3( 1, 0, 0 ), 3 );			// 4: left wall
            plane[1] = Plane( 5, float3( -1, 0, 0 ), 2.99f );		// 5: right wall
            plane[2] = Plane( 6, float3( 0, 1, 0 ), 1 );			// 6: floor
            plane[3] = Plane( 7, float3( 0, -1, 0 ), 2 );			// 7: ceiling
            plane[4] = Plane( 8, float3( 0, 0, 1 ), 3 );			// 8: front wall
            plane[5] = Plane( 9, float3( 0, 0, -1 ), 3.99f );		// 9: back wall
            torus = Torus( 10, 0.8f, 0.25f );						// 10: torus
            torus.T = mat4::Translate( -0.25f, 0, 2 ) * mat4::RotateX( PI / 4 );
            torus.invT = torus.T.Inverted();
            SetTime( 0 );
            // Note: once we have triangle support we should get rid of the class
            // hierarchy: virtuals reduce performance somewhat.
        }

        void SetTime( float t ) {
            // default time for the scene is simply 0. Updating/ the time per frame 
            // enables animation. Updating it per ray can be used for motion blur.
            animTime = t;
#ifndef FOURLIGHTS
            // light source animation: swing
            mat4 M1base = mat4::Translate( float3( 0, 2.6f, 2 ) );
            mat4 M1 = M1base * mat4::RotateZ( sinf( animTime * 0.6f ) * 0.1f ) * mat4::Translate( float3( 0, -0.9f, 0 ) );
            quad.T = M1, quad.invT = M1.FastInvertedTransformNoScale();
#endif
            // cube animation: spin
            mat4 M2base = mat4::RotateX( PI / 4 ) * mat4::RotateZ( PI / 4 );
            mat4 M2 = mat4::Translate( float3( 1.8f, 0, 2.5f ) ) * mat4::RotateY( animTime * 0.5f ) * M2base;
            cube.M = M2, cube.invM = M2.FastInvertedTransformNoScale();
            // sphere animation: bounce
            float tm = 1 - sqrf( fmodf( animTime, 2.0f ) - 1 );
            sphere.pos = float3( -1.8f, -0.4f + tm, 1 );
        }

        float3 GetLightPos() const {
#ifndef FOURLIGHTS
            // light point position is the middle of the swinging quad
            float3 corner1 = TransformPosition( float3( -0.5f, 0, -0.5f ), quad.T );
            float3 corner2 = TransformPosition( float3( 0.5f, 0, 0.5f ), quad.T );
            return ( corner1 + corner2 ) * 0.5f - float3( 0, 0.01f, 0 );
#else
            // function is not valid when using four lights; we'll return the origin
            return float3( 0 );
#endif
        }

        float3 RandomPointOnLight( const float r0, const float r1 ) const {
#ifndef FOURLIGHTS
            // get a random position on the swinging quad
            const float size = quad.size;
            float3 corner1 = TransformPosition( float3( -size, 0, -size ), quad.T );
            float3 corner2 = TransformPosition( float3( size, 0, -size ), quad.T );
            float3 corner3 = TransformPosition( float3( -size, 0, size ), quad.T );
            return corner1 + r0 * ( corner2 - corner1 ) + r1 * ( corner3 - corner1 );
#else
            // select a random light and use that
            uint lightIdx = (uint) ( r0 * 4 );
            const Quad& q = quad[lightIdx];
            // renormalize r0 for reuse
            float stratum = lightIdx * 0.25f;
            float r2 = ( r0 - stratum ) / ( 1 - stratum );
            // get a random position on the selected quad
            const float size = q.size;
            float3 corner1 = TransformPosition( float3( -size, 0, -size ), q.T );
            float3 corner2 = TransformPosition( float3( size, 0, -size ), q.T );
            float3 corner3 = TransformPosition( float3( -size, 0, size ), q.T );
            return corner1 + r2 * ( corner2 - corner1 ) + r1 * ( corner3 - corner1 );
#endif
        }

        float3 RandomPointOnLight( uint& seed ) const {
            return RandomPointOnLight( RandomFloat( seed ), RandomFloat( seed ) );
        }

        void GetLightQuad( float3& v0, float3& v1, float3& v2, float3& v3, const uint idx = 0 ) {
#ifndef FOURLIGHTS
            // return the four corners of the swinging quad, clockwise, for solid angle sampling
            const Quad& q = quad;
#else
            // return four corners of the specified light
            const Quad& q = quad[idx];
#endif
            const float size = q.size;
            v0 = TransformPosition( float3( -size, 0, size ), q.T );
            v1 = TransformPosition( float3( size, 0, size ), q.T );
            v2 = TransformPosition( float3( size, 0, -size ), q.T );
            v3 = TransformPosition( float3( -size, 0, -size ), q.T );
        }

        float3 GetLightColor() const {
            return float3( 24, 24, 22 );
        }

        float3 GetAreaLightColor() const {
#ifdef FOURLIGHTS
            return quad[0].GetAlbedo( float3( 0 ) ); // they're all the same color
#else
            return quad.GetAlbedo( float3( 0 ) );
#endif
        }

        float GetLightArea() const {
#ifdef FOURLIGHTS
            return sqrf( quad[0].size * 2 ); // all the same size
#else
            return sqrf( quad.size * 2 );
#endif
        }

        constexpr float GetLightCount() const {
#ifdef FOURLIGHTS
            return 4; // what did you expect
#else
            return 1;
#endif
        }

        void FindNearest( Ray& ray ) const {
            // room walls - ugly shortcut for more speed
#ifdef SPEEDTRIX
            // prefetching
            const float3 spos = sphere.pos;
            const float3 ro = ray.O;
            const float3 rd = ray.D;
            float t;
            if ( ray.D.x < 0 ) PLANE_X( 3, 4 ) else PLANE_X( -2.99f, 5 );
            if ( ray.D.y < 0 ) PLANE_Y( 1, 6 ) else PLANE_Y( -2, 7 );
            if ( ray.D.z < 0 ) PLANE_Z( 3, 8 ) else PLANE_Z( -3.99f, 9 );
#else
            if ( ray.D.x < 0 ) PLANE_X( 3, 4 ) else PLANE_X( -2.99f, 5 );
            if ( ray.D.y < 0 ) PLANE_Y( 1, 6 ) else PLANE_Y( -2, 7 );
            if ( ray.D.z < 0 ) PLANE_Z( 3, 8 ) else PLANE_Z( -3.99f, 9 );
#endif
#ifdef FOURLIGHTS
            {
                const __m128 tq4 = _mm_div_ps( _mm_add_ps( _mm_set1_ps( ray.O.y ), _mm_set1_ps( -1.5f ) ), _mm_xor_ps( _mm_set1_ps( ray.D.y ), _mm_set1_ps( -0.0f ) ) );
                const __m128 Ix4 = _mm_add_ps( _mm_add_ps( _mm_set1_ps( ray.O.x ), { 1, -1, -1, 1 } ), _mm_mul_ps( tq4, _mm_set1_ps( ray.D.x ) ) );
                const __m128 Iz4 = _mm_add_ps( _mm_add_ps( _mm_set1_ps( ray.O.z ), { 1, 1, -1, -1 } ), _mm_mul_ps( tq4, _mm_set1_ps( ray.D.z ) ) );
                const __m128 hitmask = _mm_and_ps( _mm_and_ps( _mm_cmpgt_ps( Ix4, _mm_set1_ps( -0.25f ) ), _mm_cmplt_ps( Ix4, _mm_set1_ps( 0.25f ) ) ), _mm_and_ps( _mm_cmpgt_ps( Iz4, _mm_set1_ps( -0.25f ) ), _mm_cmplt_ps( Iz4, _mm_set1_ps( 0.25f ) ) ) );
                const __m128 valmask = _mm_and_ps( _mm_cmplt_ps( tq4, _mm_set_ps1( ray.t ) ), _mm_cmpgt_ps( tq4, _mm_setzero_ps() ) );
                const __m128 mask = _mm_and_ps( hitmask, valmask );
                const __m128 ft4 = _mm_blendv_ps( _mm_set_ps1( 1e34f ), tq4, mask );
                if ( ft4.m128_f32[0] < ray.t ) ray.t = ft4.m128_f32[0], ray.objIdx = 0;
                if ( ft4.m128_f32[1] < ray.t ) ray.t = ft4.m128_f32[1], ray.objIdx = 0;
                if ( ft4.m128_f32[2] < ray.t ) ray.t = ft4.m128_f32[2], ray.objIdx = 0;
                if ( ft4.m128_f32[3] < ray.t ) ray.t = ft4.m128_f32[3], ray.objIdx = 0;
            }
#else
            quad.Intersect( ray );
#endif
#ifdef SPEEDTRIX // hardcoded spheres, a bit faster this way but very ugly
            {
                const float3 oc = ro - spos;
                const float b = dot( oc, rd );
                const float c = dot( oc, oc ) - ( 0.6f * 0.6f );
                const float d = b * b - c;
                if ( d > 0 ) {
                    const float t = -b - sqrtf( d );
                    const bool hit = t < ray.t && t > 0;
                    if ( hit ) ray.t = t, ray.objIdx = 1;
                }
            }
            {
                const float3 oc = ro - float3( 0, 2.5f, -3.07f );
                const float b = dot( oc, rd );
                const float c = dot( oc, oc ) - ( 8 * 8 );
                const float d = b * b - c;
                if ( d > 0 ) {
                    float t = sqrtf( d ) - b;
                    const bool hit = t < ray.t && t > 0;
                    if ( hit ) ray.t = t, ray.objIdx = 2;
                }
            }
#else
            sphere.Intersect( ray );
            sphere2.Intersect( ray );
#endif
            cube.Intersect( ray );
            torus.Intersect( ray );
        }

        bool IsOccluded( const Ray& ray ) const {
            if ( cube.IsOccluded( ray ) ) return true;
#ifdef SPEEDTRIX
            const float3 oc = ray.O - sphere.pos;
            const float b = dot( oc, ray.D );
            const float c = dot( oc, oc ) - ( 0.6f * 0.6f );
            const float d = b * b - c;
            if ( d > 0 ) {
                const float t = -b - sqrtf( d );
                const bool hit = t < ray.t && t > 0;
                if ( hit ) return true;
            }
#else
            if ( sphere.IsOccluded( ray ) ) return true;
#endif
#ifdef FOURLIGHTS
            {
                const __m128 tq4 = _mm_div_ps( _mm_add_ps( _mm_set1_ps( ray.O.y ), _mm_set1_ps( -1.5f ) ), _mm_xor_ps( _mm_set1_ps( ray.D.y ), _mm_set1_ps( -0.0f ) ) );
                const __m128 Ix4 = _mm_add_ps( _mm_add_ps( _mm_set1_ps( ray.O.x ), { 1, -1, -1, 1 } ), _mm_mul_ps( tq4, _mm_set1_ps( ray.D.x ) ) );
                const __m128 Iz4 = _mm_add_ps( _mm_add_ps( _mm_set1_ps( ray.O.z ), { 1, 1, -1, -1 } ), _mm_mul_ps( tq4, _mm_set1_ps( ray.D.z ) ) );
                const __m128 hitmask = _mm_and_ps( _mm_and_ps( _mm_cmpgt_ps( Ix4, _mm_set1_ps( -0.25f ) ), _mm_cmplt_ps( Ix4, _mm_set1_ps( 0.25f ) ) ), _mm_and_ps( _mm_cmpgt_ps( Iz4, _mm_set1_ps( -0.25f ) ), _mm_cmplt_ps( Iz4, _mm_set1_ps( 0.25f ) ) ) );
                const __m128 valmask = _mm_and_ps( _mm_cmplt_ps( tq4, _mm_set_ps1( ray.t ) ), _mm_cmpgt_ps( tq4, _mm_setzero_ps() ) );
                int hit = _mm_movemask_ps( _mm_and_ps( hitmask, valmask ) );
                if ( hit != 0 ) return true;
            }
#else
            if ( quad.IsOccluded( ray ) ) return true;
#endif
            if ( torus.IsOccluded( ray ) ) return true;
            return false; // skip planes and rounded corners
        }

        float3 GetNormal( const int objIdx, const float3 I, const float3 wo ) const {
            // we get the normal after finding the nearest intersection:
            // this way we prevent calculating it multiple times.
            if ( objIdx == -1 ) return float3( 0 ); // or perhaps we should just crash
            float3 N;
#ifdef FOURLIGHTS
            if ( objIdx == 0 ) N = quad[0].GetNormal( I ); // they're all oriented the same
#else
            if ( objIdx == 0 ) N = quad.GetNormal( I );
#endif
            else if ( objIdx == 1 ) N = sphere.GetNormal( I );
            else if ( objIdx == 2 ) N = sphere2.GetNormal( I );
            else if ( objIdx == 3 ) N = cube.GetNormal( I );
            else if ( objIdx == 10 ) N = torus.GetNormal( I );
            else {
                // faster to handle the 6 planes without a call to GetNormal
                N = float3( 0 );
                N[( objIdx - 4 ) / 2] = 1 - 2 * (float) ( objIdx & 1 );
            }

            if ( dot( N, wo ) > 0 ) N = -N; // hit backside / inside
            return N;
        }

        float3 GetAlbedo( int objIdx, float3 I ) const {
            if ( objIdx == -1 ) return float3( 0 ); // or perhaps we should just crash
#ifdef FOURLIGHTS
            if ( objIdx == 0 ) return quad[0].GetAlbedo( I ); // they're all the same
#else
            if ( objIdx == 0 ) return quad.GetAlbedo( I );
#endif
            if ( objIdx == 1 ) return sphere.GetAlbedo( I );
            if ( objIdx == 2 ) return sphere2.GetAlbedo( I );
            if ( objIdx == 3 ) return cube.GetAlbedo( I );
            if ( objIdx == 10 ) return torus.GetAlbedo( I );
            return plane[objIdx - 4].GetAlbedo( I );
            // once we have triangle support, we should pass objIdx and the bary-
            // centric coordinates of the hit, instead of the intersection location.
        }

        float GetReflectivity( int objIdx, float3 I ) const {
            if ( objIdx == 1 /* ball */ ) return 1;
            if ( objIdx == 6 /* floor */ ) return 0.3f;
            return 0;
        }

        float GetRefractivity( int objIdx, float3 I ) const {
            return ( objIdx == 3 || objIdx == 10 ) ? 1.0f : 0.0f;
        }

        float3 GetAbsorption( int objIdx ) {
            return objIdx == 3 ? float3( 0.5f, 0, 0.5f ) : float3( 0 );
        }

        __declspec( align( 64 ) ) // start a new cacheline here
        float animTime = 0;
#ifdef FOURLIGHTS
        Quad quad[4];
#else
        Quad quad, dummyQuad1, dummyQuad2, dummyQuad3;
#endif
        Sphere sphere;
        Sphere sphere2;
        Cube cube;
        Plane plane[6];
        Torus torus;
    };

}
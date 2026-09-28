/**
 * Test-only reference: routines copied from the Second Life viewer so VATs' own code can be
 * checked against them bit for bit. Sources (secondlife/viewer, indra/llmath):
 *   llquantize.h      F32_to_U16, U16_to_F32
 *   llquaternion.cpp  LLQuaternion(angle, axis), operator*, packToVector3, unpackFromVector3,
 *                     setQuat(roll, pitch, yaw), mayaQ
 *   v3math.cpp        LLVector3::quantize16
 * Bodies are unchanged; only the surrounding types are reduced to plain structs.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace viewer_ref {

typedef float F32;
typedef std::uint16_t U16;
typedef std::int32_t S32;
enum { VX = 0, VY = 1, VZ = 2, VW = 3 };

const U16 U16MAX = 65535;
const F32 OOU16MAX = 1.f / (F32)(U16MAX);
constexpr F32 FP_MAG_THRESHOLD = 0.0000001f;
constexpr F32 DEG_TO_RAD = 0.017453292519943295769236907684886f;

template <typename T>
inline T llclamp(const T& a, const T& minval, const T& maxval) {
    if (a < minval) return minval;
    if (a > maxval) return maxval;
    return a;
}

inline S32 llfloor(F32 f) { return (S32)floor(f); }

inline U16 F32_to_U16(F32 val, F32 lower, F32 upper)
{
    val = llclamp(val, lower, upper);
    // make sure that the value is positive and normalized to <0, 1>
    val -= lower;
    val /= (upper - lower);

    // return the U16
    return (U16)(llfloor(val*U16MAX));
}

inline F32 U16_to_F32(U16 ival, F32 lower, F32 upper)
{
    F32 val = ival*OOU16MAX;
    F32 delta = (upper - lower);
    val *= delta;
    val += lower;

    F32 max_error = delta*OOU16MAX;

    // make sure that zero's come through as zero
    if (fabsf(val) < max_error)
        val = 0.f;

    return val;
}

struct LLVector3 {
    F32 mV[3] = {0, 0, 0};
    LLVector3() = default;
    LLVector3(F32 x, F32 y, F32 z) { mV[0] = x; mV[1] = y; mV[2] = z; }

    void quantize16(F32 lowerxy, F32 upperxy, F32 lowerz, F32 upperz)
    {
        F32 x = mV[VX];
        F32 y = mV[VY];
        F32 z = mV[VZ];

        x = U16_to_F32(F32_to_U16(x, lowerxy, upperxy), lowerxy, upperxy);
        y = U16_to_F32(F32_to_U16(y, lowerxy, upperxy), lowerxy, upperxy);
        z = U16_to_F32(F32_to_U16(z, lowerz,  upperz),  lowerz,  upperz);

        mV[VX] = x;
        mV[VY] = y;
        mV[VZ] = z;
    }
};

struct LLQuaternion {
    F32 mQ[4] = {0, 0, 0, 1};
    LLQuaternion() = default;
    LLQuaternion(F32 x, F32 y, F32 z, F32 w) { mQ[0] = x; mQ[1] = y; mQ[2] = z; mQ[3] = w; }
    void loadIdentity() { mQ[0] = mQ[1] = mQ[2] = 0; mQ[3] = 1; }

    LLQuaternion(F32 angle, const LLVector3 &vec)
    {
        F32 mag = sqrtf(vec.mV[VX] * vec.mV[VX] + vec.mV[VY] * vec.mV[VY] + vec.mV[VZ] * vec.mV[VZ]);
        if (mag > FP_MAG_THRESHOLD)
        {
            angle *= 0.5;
            F32 c = cosf(angle);
            F32 s = sinf(angle) / mag;
            mQ[VX] = vec.mV[VX] * s;
            mQ[VY] = vec.mV[VY] * s;
            mQ[VZ] = vec.mV[VZ] * s;
            mQ[VW] = c;
        }
        else
        {
            loadIdentity();
        }
    }

    const LLQuaternion& setQuat(F32 roll, F32 pitch, F32 yaw)
    {
        roll  *= 0.5f;
        pitch *= 0.5f;
        yaw   *= 0.5f;
        F32 sinX = sinf(roll);
        F32 cosX = cosf(roll);
        F32 sinY = sinf(pitch);
        F32 cosY = cosf(pitch);
        F32 sinZ = sinf(yaw);
        F32 cosZ = cosf(yaw);
        mQ[VW] = cosX * cosY * cosZ - sinX * sinY * sinZ;
        mQ[VX] = sinX * cosY * cosZ + cosX * sinY * sinZ;
        mQ[VY] = cosX * sinY * cosZ - sinX * cosY * sinZ;
        mQ[VZ] = cosX * cosY * sinZ + sinX * sinY * cosZ;
        return (*this);
    }

    LLVector3 packToVector3() const
    {
        F32 x = mQ[VX];
        F32 y = mQ[VY];
        F32 z = mQ[VZ];
        F32 w = mQ[VW];
        F32 mag = sqrtf(x * x + y * y + z * z + w * w);
        if (mag > FP_MAG_THRESHOLD)
        {
            x /= mag;
            y /= mag;
            z /= mag; // no need to normalize w, it's not used
        }
        if( mQ[VW] >= 0 )
        {
            return LLVector3( x, y , z );
        }
        else
        {
            return LLVector3( -x, -y, -z );
        }
    }

    void unpackFromVector3( const LLVector3& vec )
    {
        mQ[VX] = vec.mV[VX];
        mQ[VY] = vec.mV[VY];
        mQ[VZ] = vec.mV[VZ];
        F32 t = 1.f - (vec.mV[0] * vec.mV[0] + vec.mV[1] * vec.mV[1] + vec.mV[2] * vec.mV[2]);
        if( t > 0 )
        {
            mQ[VW] = sqrt( t );
        }
        else
        {
            // Need this to avoid trying to find the square root of a negative number due
            // to floating point error.
            mQ[VW] = 0;
        }
    }
};

inline LLQuaternion operator*(const LLQuaternion &a, const LLQuaternion &b)
{
    LLQuaternion q(
        b.mQ[3] * a.mQ[0] + b.mQ[0] * a.mQ[3] + b.mQ[1] * a.mQ[2] - b.mQ[2] * a.mQ[1],
        b.mQ[3] * a.mQ[1] + b.mQ[1] * a.mQ[3] + b.mQ[2] * a.mQ[0] - b.mQ[0] * a.mQ[2],
        b.mQ[3] * a.mQ[2] + b.mQ[2] * a.mQ[3] + b.mQ[0] * a.mQ[1] - b.mQ[1] * a.mQ[0],
        b.mQ[3] * a.mQ[3] - b.mQ[0] * a.mQ[0] - b.mQ[1] * a.mQ[1] - b.mQ[2] * a.mQ[2]
    );
    return q;
}

enum Order { XYZ = 0, YZX = 1, ZXY = 2, XZY = 3, YXZ = 4, ZYX = 5 };

inline LLQuaternion mayaQ(F32 xRot, F32 yRot, F32 zRot, Order order)
{
    LLQuaternion xQ( xRot*DEG_TO_RAD, LLVector3(1.0f, 0.0f, 0.0f) );
    LLQuaternion yQ( yRot*DEG_TO_RAD, LLVector3(0.0f, 1.0f, 0.0f) );
    LLQuaternion zQ( zRot*DEG_TO_RAD, LLVector3(0.0f, 0.0f, 1.0f) );
    LLQuaternion ret;
    switch( order )
    {
    case XYZ:
        ret = xQ * yQ * zQ;
        break;
    case YZX:
        ret = yQ * zQ * xQ;
        break;
    case ZXY:
        ret = zQ * xQ * yQ;
        break;
    case XZY:
        ret = xQ * zQ * yQ;
        break;
    case YXZ:
        ret = yQ * xQ * zQ;
        break;
    case ZYX:
        ret = zQ * yQ * xQ;
        break;
    }
    return ret;
}

// The rotation-key packing sequence from LLKeyframeMotion::serialize.
inline void serialize_rotation(const LLQuaternion& rot, U16& x, U16& y, U16& z)
{
    LLVector3 rot_angles = rot.packToVector3();
    rot_angles.quantize16(-1.f, 1.f, -1.f, 1.f);
    x = F32_to_U16(rot_angles.mV[VX], -1.f, 1.f);
    y = F32_to_U16(rot_angles.mV[VY], -1.f, 1.f);
    z = F32_to_U16(rot_angles.mV[VZ], -1.f, 1.f);
}

}  // namespace viewer_ref
